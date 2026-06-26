#!/usr/bin/env bash
# Create the dense-retrieval venv and install deps from PUBLIC PyPI.
#
# We pass --index-url explicitly so this works even on machines whose pip is
# pinned to a private index (this one's was — see the project notes). Run from repo root:
#     bash dense/setup_env.sh
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
VENV="$HERE/.venv"

if [ ! -d "$VENV" ]; then
  python3 -m venv "$VENV"
fi

"$VENV/bin/python" -m pip install --upgrade pip
"$VENV/bin/python" -m pip install --index-url https://pypi.org/simple/ -r "$HERE/requirements.txt"

echo
echo "Dense venv ready: $VENV"
"$VENV/bin/python" - <<'PY'
import torch
print("torch", torch.__version__, "| MPS available:", torch.backends.mps.is_available())
PY
