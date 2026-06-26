#!/usr/bin/env bash
# Create the gateway venv, install deps from public PyPI, and generate the Python
# gRPC stubs from proto/joho.proto. One-time setup.
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/.." && pwd)"

python3 -m venv "$HERE/.venv"
PY="$HERE/.venv/bin/python"
"$PY" -m pip install --upgrade pip
"$PY" -m pip install --index-url https://pypi.org/simple/ -r "$HERE/requirements.txt"

echo "==> generating Python gRPC stubs"
PYTHON="$PY" bash "$ROOT/scripts/gen_proto.sh"

echo
echo "Gateway ready. Start the engine first, then the gateway:"
echo "  # 1) engine (needs a built joho_server, or run lexical-only via in-RAM index)"
echo "  engine/build/joho_server --corpus data/beir_scifact_test/corpus.tsv --port 50051"
echo "  # 2) gateway"
echo "  cd gateway && .venv/bin/uvicorn app:app --host 0.0.0.0 --port 8000"
