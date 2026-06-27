#!/usr/bin/env bash
# MS MARCO ingestion + BM25 eval job — AWS variant. Runs ON the ephemeral EC2 launched
# by deploy/aws/10_ec2_job.sh, which copies the repo and exports BUCKET + region.
#
# MS MARCO passage is ~8.8M passages — too big to index comfortably on a local dev
# machine, which is why this runs on a 32 GB instance. Same P1 eval harness validated
# on SciFact, pointed at a bigger dataset:
#   export (ir_datasets) -> joho_batch BM25 -> ir_measures grade -> push to S3.
# Expected ballpark on dev/small: MRR@10 ~ 0.187 (a healthy from-scratch BM25 baseline).
#
# Run via:  deploy/aws/10_ec2_job.sh deploy/aws/marco_job.sh
set -euo pipefail

DATASET="${DATASET:-msmarco-passage/dev/small}"
DOCS_DATASET="${DOCS_DATASET:-msmarco-passage}"
BUCKET="${BUCKET:?BUCKET (bare S3 bucket name) must be set by the launcher}"
WORK="data/msmarco_passage_dev_small"

echo "==> [1/5] installing build toolchain + awscli"
sudo apt-get update -qq
sudo apt-get install -y -qq build-essential cmake clang git python3-venv python3-pip awscli

# Optional swap — lets the in-memory index build fit on small-RAM instances (e.g. the
# 8 GB free-tier box) without an OOM kill. Spills are slower but the job completes.
if [ "${SWAP_GB:-0}" -gt 0 ] && ! swapon --show | grep -q /swapfile; then
  echo "    provisioning ${SWAP_GB}G swap"
  sudo fallocate -l "${SWAP_GB}G" /swapfile 2>/dev/null \
    || sudo dd if=/dev/zero of=/swapfile bs=1M count=$((SWAP_GB*1024)) status=none
  sudo chmod 600 /swapfile && sudo mkswap /swapfile >/dev/null && sudo swapon /swapfile
  free -h | sed 's/^/    /'
fi

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

echo "==> [5/5] pushing index + run + scorecard to s3://$BUCKET"
# Keep the heavy artifacts (corpus.tsv can be ~3 GB) so we never re-export.
aws s3 sync "$WORK" "s3://$BUCKET/msmarco_passage_dev_small"

echo "Done. Scorecard:"
cat "$WORK/scorecard.txt"
