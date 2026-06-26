# Joho — combined single-container image for Cloud Run (P8).
#
# Cloud Run bills per-request and scales to zero, so the cheapest live demo is ONE
# service. This image runs the C++ gRPC engine and the FastAPI gateway side by side;
# the gateway reaches the engine on localhost, so there's no inter-service auth, no
# second cold start, and idle cost is ~$0. (The split engine/gateway images —
# Dockerfile.engine / Dockerfile.gateway + docker-compose — are for local dev and the
# horizontally-scaled story.)

# ---- build the C++ engine (with gRPC) ----
FROM debian:bookworm-slim AS build
RUN apt-get update && apt-get install -y --no-install-recommends \
      build-essential cmake protobuf-compiler-grpc libgrpc++-dev libprotobuf-dev \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /src
COPY engine/ engine/
COPY proto/ proto/
RUN cmake -S engine -B engine/build -DCMAKE_BUILD_TYPE=Release \
    && cmake --build engine/build -j --target joho_server

# ---- python runtime with the engine binary alongside ----
FROM python:3.12-slim
WORKDIR /app
RUN apt-get update && apt-get install -y --no-install-recommends \
      libgrpc++1.51 libprotobuf32 ca-certificates \
    && rm -rf /var/lib/apt/lists/*
COPY --from=build /src/engine/build/joho_server /usr/local/bin/joho_server

COPY gateway/requirements.txt .
RUN pip install --no-cache-dir --index-url https://pypi.org/simple/ -r requirements.txt \
    && pip install --no-cache-dir grpcio-tools
COPY proto/ /app/proto/
COPY gateway/ /app/
RUN python -m grpc_tools.protoc --proto_path=/app/proto \
      --python_out=/app/joho_pb --grpc_python_out=/app/joho_pb /app/proto/joho.proto \
    && touch /app/joho_pb/__init__.py \
    && sed -i 's/^import joho_pb2 as/from . import joho_pb2 as/' /app/joho_pb/joho_pb2_grpc.py

# Baked-in demo dataset (corpus + precomputed doc embeddings).
COPY data/beir_scifact_test/corpus.tsv /data/beir_scifact_test/corpus.tsv
COPY data/beir_scifact_test/corpus_bge.npy /data/beir_scifact_test/corpus_bge.npy
COPY data/beir_scifact_test/corpus_bge.ids.txt /data/beir_scifact_test/corpus_bge.ids.txt
RUN python -c "from sentence_transformers import SentenceTransformer; SentenceTransformer('BAAI/bge-small-en-v1.5')"

COPY deploy/entrypoint_combined.sh /entrypoint.sh
RUN chmod +x /entrypoint.sh

ENV JOHO_CORPUS=/data/beir_scifact_test/corpus.tsv \
    JOHO_DATASET_DIR=/data/beir_scifact_test \
    JOHO_ENGINE_ADDR=localhost:50051 \
    JOHO_ENABLE_DENSE=1 \
    JOHO_ENABLE_RERANK=0 \
    HF_HOME=/app/.hf \
    PORT=8080
EXPOSE 8080
CMD ["/entrypoint.sh"]
