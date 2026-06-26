#!/usr/bin/env bash
# Generate gRPC stubs from proto/joho.proto for both languages (P6).
#
#   C++    -> engine/build/gen/         (also done automatically by CMake)
#   Python -> gateway/joho_pb/          (joho_pb2.py + joho_pb2_grpc.py)
#
# The C++ side needs `protoc` + `grpc_cpp_plugin` on PATH (brew install grpc protobuf).
# The Python side uses grpcio-tools from the gateway venv (created by gateway/setup_env.sh).
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PROTO_DIR="$ROOT/proto"
PROTO="$PROTO_DIR/joho.proto"

echo "==> Python stubs -> gateway/joho_pb/"
PY="${PYTHON:-$ROOT/gateway/.venv/bin/python}"
if [ ! -x "$PY" ]; then PY="$(command -v python3)"; fi
mkdir -p "$ROOT/gateway/joho_pb"
"$PY" -m grpc_tools.protoc \
  --proto_path="$PROTO_DIR" \
  --python_out="$ROOT/gateway/joho_pb" \
  --grpc_python_out="$ROOT/gateway/joho_pb" \
  "$PROTO"
# Make the generated package importable and fix the absolute import grpc_tools emits.
touch "$ROOT/gateway/joho_pb/__init__.py"
# joho_pb2_grpc.py does `import joho_pb2` — rewrite to a package-relative import.
if [ -f "$ROOT/gateway/joho_pb/joho_pb2_grpc.py" ]; then
  sed -i.bak 's/^import joho_pb2 as/from . import joho_pb2 as/' \
    "$ROOT/gateway/joho_pb/joho_pb2_grpc.py" && rm -f "$ROOT/gateway/joho_pb/joho_pb2_grpc.py.bak"
fi
echo "    done."

echo "==> C++ stubs -> engine/build/gen/  (optional; CMake also generates these)"
if command -v protoc >/dev/null && command -v grpc_cpp_plugin >/dev/null; then
  mkdir -p "$ROOT/engine/build/gen"
  protoc --proto_path="$PROTO_DIR" \
    --cpp_out="$ROOT/engine/build/gen" \
    --grpc_out="$ROOT/engine/build/gen" \
    --plugin=protoc-gen-grpc="$(command -v grpc_cpp_plugin)" \
    "$PROTO"
  echo "    done."
else
  echo "    skipped (protoc/grpc_cpp_plugin not on PATH; install: brew install grpc protobuf)"
fi
