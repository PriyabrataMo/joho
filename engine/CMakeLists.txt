cmake_minimum_required(VERSION 3.16)
project(joho_engine LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Emit compile_commands.json so editors (VS Code C/C++, clangd) can resolve
# #include paths and stop showing false "cannot open source file" errors.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Default to an optimized build unless told otherwise.
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# The reusable core: tokenizer + inverted index + BM25 + on-disk index, plus the
# P5 query-assist (trie + SymSpell) and P7 sharded searcher. Kept as a library so
# the eval harness, the CLIs, and the gRPC server all link the same code.
add_library(joho_core
  src/tokenizer.cpp
  src/inverted_index.cpp
  src/bm25.cpp
  src/corpus.cpp
  src/disk_index.cpp
  src/trie.cpp
  src/symspell.cpp
  src/suggest_index.cpp
  src/sharded_searcher.cpp)
target_include_directories(joho_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_options(joho_core PRIVATE -Wall -Wextra)

# joho_core uses std::thread (sharded_searcher) — link the platform threads lib.
find_package(Threads REQUIRED)
target_link_libraries(joho_core PUBLIC Threads::Threads)

# The toy demo executable (tiny built-in corpus, for seeing BM25 work).
add_executable(joho src/main.cpp)
target_link_libraries(joho PRIVATE joho_core)

# The batch retrieval driver: corpus.tsv + queries.tsv -> TREC run file. This is
# what the Python eval harness calls to reproduce BM25 baselines (P1).
add_executable(joho_batch src/batch.cpp)
target_link_libraries(joho_batch PRIVATE joho_core)

# The offline index builder: corpus.tsv -> compressed on-disk index, optionally
# sharded (P2 / P7).
add_executable(joho_build src/build_index.cpp)
target_link_libraries(joho_build PRIVATE joho_core)

# Sharded scatter-gather retrieval driver (P7).
add_executable(joho_shard_search src/shard_search.cpp)
target_link_libraries(joho_shard_search PRIVATE joho_core)

# Autocomplete + spell-correction driver (P5).
add_executable(joho_suggest src/suggest.cpp)
target_link_libraries(joho_suggest PRIVATE joho_core)

# Unit tests (dependency-free; each returns non-zero on failure).
enable_testing()
add_executable(test_varint tests/test_varint.cpp)
target_link_libraries(test_varint PRIVATE joho_core)
add_test(NAME varint COMMAND test_varint)

add_executable(test_trie tests/test_trie.cpp)
target_link_libraries(test_trie PRIVATE joho_core)
add_test(NAME trie COMMAND test_trie)

add_executable(test_symspell tests/test_symspell.cpp)
target_link_libraries(test_symspell PRIVATE joho_core)
add_test(NAME symspell COMMAND test_symspell)

# ---------------------------------------------------------------------------
# Optional: gRPC server (P6). The engine exposes Search / Autocomplete / Correct
# over gRPC so the Python gateway can orchestrate the funnel. This target builds
# ONLY if gRPC + Protobuf are installed (e.g. `brew install grpc protobuf`), so the
# rest of the engine always builds even on a machine without them.
# ---------------------------------------------------------------------------
find_package(Protobuf CONFIG QUIET)
find_package(gRPC CONFIG QUIET)
find_program(GRPC_CPP_PLUGIN grpc_cpp_plugin)
find_program(PROTOC_BIN protoc)

if(Protobuf_FOUND AND gRPC_FOUND AND GRPC_CPP_PLUGIN AND PROTOC_BIN)
  message(STATUS "gRPC + Protobuf found -> building joho_server (P6)")
  set(PROTO_FILE ${CMAKE_CURRENT_SOURCE_DIR}/../proto/joho.proto)
  get_filename_component(PROTO_DIR ${PROTO_FILE} DIRECTORY)
  set(GEN_DIR ${CMAKE_CURRENT_BINARY_DIR}/gen)
  file(MAKE_DIRECTORY ${GEN_DIR})
  set(PB_SRCS ${GEN_DIR}/joho.pb.cc ${GEN_DIR}/joho.grpc.pb.cc)
  set(PB_HDRS ${GEN_DIR}/joho.pb.h ${GEN_DIR}/joho.grpc.pb.h)
  add_custom_command(
    OUTPUT ${PB_SRCS} ${PB_HDRS}
    COMMAND ${PROTOC_BIN}
            --proto_path=${PROTO_DIR}
            --cpp_out=${GEN_DIR}
            --grpc_out=${GEN_DIR}
            --plugin=protoc-gen-grpc=${GRPC_CPP_PLUGIN}
            ${PROTO_FILE}
    DEPENDS ${PROTO_FILE}
    COMMENT "Generating C++ gRPC stubs from joho.proto")
  add_executable(joho_server src/server.cpp ${PB_SRCS})
  target_include_directories(joho_server PRIVATE ${GEN_DIR})
  target_link_libraries(joho_server PRIVATE joho_core gRPC::grpc++ protobuf::libprotobuf)
else()
  message(STATUS "gRPC/Protobuf not found -> skipping joho_server "
                 "(install with: brew install grpc protobuf). Everything else still builds.")
endif()
