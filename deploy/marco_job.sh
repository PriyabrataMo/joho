#!/usr/bin/env bash
# MS MARCO ingestion + BM25 eval job (P8). Designed to run ON the ephemeral Spot VM
# launched by 30_vm_job.sh, which copies the repo and exports BUCKET before calling us.
#
# MS MARCO passage is ~8.8M passages — too big to index comfortably on a local dev machine,
# which is exactly why this runs on a 32 GB cloud VM. The flow reuses the same P1 eval
# harness we validated on SciFact, just pointed at a bigger dataset:
#   export (ir_datasets) -> joho_batch BM25 -> ir_measures grade -> push to GCS.
# Expected ballpark on dev/small: MRR@10 ~ 0.187 (a healthy from-scratch BM25 baseline).
#
# Run via:  deploy/30_vm_job.sh deploy/marco_job.sh
# (BUCKET comes from the launcher; falls back to a sane default for standalone runs.)
set -euo pipefail

DATASET="${DATASET:-msmarco-passage/dev/small}"
DOCS_DATASET="${DOCS_DATASET:-msmarco-passage}"
BUCKET="${BUCKET:-gs://joho-search-joho-data}"
WORK="data/msmarco_passage_dev_small"

echo "==> [1/5] installing build toolchain"
sudo apt-get update -qq
sudo apt-get install -y -qq build-essential cmake clang git python3-venv python3-pip

echo "==> [2/5] building the C++ engine (Release)"
cmake -S engine -B engine/build -DCMAKE_BUILD_TYPE=Release >/dev/null
cmake --build engine/build -j"$(nproc)" --target joho_batch joho_build

echo "==> [3/5] eval venv"
bash eval/setup_env.sh

echo "==> [4/5] export + index + grade MS MARCO ($DATASET)"
mkdir -p "$WORK"   # so the tee target exists before run_eval.py populates the dir
# run_eval.py: export corpus+queries -> joho_batch BM25 run -> ir_measures scorecard.
eval/.venv/bin/python eval/run_eval.py \
  --dataset "$DATASET" \
  --docs-dataset "$DOCS_DATASET" \
  --top-k 1000 --k1 0.9 --b 0.4 \
  --tag joho-bm25-marco | tee "$WORK/scorecard.txt"

echo "==> [5/5] pushing index + run + scorecard to $BUCKET"
# Keep the heavy artifacts (corpus.tsv can be ~3 GB) so we never re-export.
gcloud storage rsync -r "$WORK" "$BUCKET/msmarco_passage_dev_small"

echo "Done. Scorecard:"
cat "$WORK/scorecard.txt"
