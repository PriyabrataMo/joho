#!/usr/bin/env bash
# Create the eval harness's isolated Python environment.
#
# IMPORTANT: this machine's global pip config used to point at a private work
# package index (now disabled). To be 100% robust regardless of any global pip
# config, we install EXPLICITLY from public PyPI with --index-url. The venv keeps
# Joho's Python deps separate from system / work Python.
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$HERE"

PYPI="https://pypi.org/simple/"

echo "Creating venv at eval/.venv ..."
python3 -m venv .venv

echo "Upgrading pip (from public PyPI) ..."
.venv/bin/python -m pip install --quiet --upgrade --index-url "$PYPI" pip

echo "Installing eval dependencies (from public PyPI) ..."
.venv/bin/python -m pip install --index-url "$PYPI" -r requirements.txt

echo ""
echo "Done. Activate with:   source eval/.venv/bin/activate"
echo "Or just run the harness with eval/.venv/bin/python (no activation needed)."
