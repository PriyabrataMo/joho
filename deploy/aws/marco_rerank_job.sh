#!/usr/bin/env bash
# MS MARCO BM25 + corpus-subset export for HYBRID re-ranking — AWS variant.
#
# This is the cloud half of the two-stage funnel. The ephemeral EC2 box does the
# disk-heavy work — download MS MARCO (~3 GB), export the 8.8M-passage corpus,
# build the index, run BM25 — and then ships back ONLY the small artifacts the
# laptop needs to re-rank on its GPU (Apple MPS):
#
#   * run.txt            BM25 first-stage run (top-1000 per query, ~350 MB)
#   * queries.tsv        query text (tiny)
#   * corpus_topN.tsv    ONLY the passages that appear in some query's top-N
#                        (a few hundred MB, not the full 2.6 GB corpus)
#   * scorecard.txt      the BM25 baseline metrics (re-confirms MRR@10 ~0.182)
#
# The cross-encoder re-rank itself runs on the M4 (MPS), where 700k (query,doc)
# forward passes take ~10-15 min instead of ~1.5 h on a 2-vCPU CPU box.
#
# Run via:  S3_PREFIX=msmarco_rerank deploy/aws/10_ec2_job.sh deploy/aws/marco_rerank_job.sh
set -euo pipefail

DATASET="${DATASET:-msmarco-passage/dev/small}"
DOCS_DATASET="${DOCS_DATASET:-msmarco-passage}"
BUCKET="${BUCKET:?BUCKET (bare S3 bucket name) must be set by the launcher}"
DEST="${DEST:-msmarco_rerank}"          # S3 sub-prefix for the hybrid artifacts
DEPTH="${RERANK_DEPTH:-100}"            # how deep the laptop will re-rank
WORK="data/msmarco_rerank"             # single work dir (pinned via --work-dir below)

echo "==> [1/6] installing build toolchain + awscli"
sudo apt-get update -qq
sudo apt-get install -y -qq build-essential cmake clang git python3-venv python3-pip awscli

# Optional swap — lets the in-memory index build fit on small-RAM instances without
# an OOM kill. Spills are slower but the job completes.
if [ "${SWAP_GB:-0}" -gt 0 ] && ! swapon --show | grep -q /swapfile; then
  echo "    provisioning ${SWAP_GB}G swap"
  sudo fallocate -l "${SWAP_GB}G" /swapfile 2>/dev/null \
    || sudo dd if=/dev/zero of=/swapfile bs=1M count=$((SWAP_GB*1024)) status=none
  sudo chmod 600 /swapfile && sudo mkswap /swapfile >/dev/null && sudo swapon /swapfile
  free -h | sed 's/^/    /'
fi

echo "==> [2/6] building the C++ engine (Release)"
cmake -S engine -B engine/build -DCMAKE_BUILD_TYPE=Release >/dev/null
cmake --build engine/build -j"$(nproc)" --target joho_batch joho_build

echo "==> [3/6] eval venv"
bash eval/setup_env.sh

echo "==> [4/6] export + index + grade MS MARCO ($DATASET)"
mkdir -p "$WORK"
# --work-dir pins corpus.tsv / queries.tsv / run.txt INTO $WORK (the default slug dir
# is 'msmarco-passage_dev_small' with a hyphen, which previously diverged from the
# upload dir and left run.txt/corpus.tsv un-synced). Everything lands together now.
eval/.venv/bin/python eval/run_eval.py \
  --dataset "$DATASET" \
  --docs-dataset "$DOCS_DATASET" \
  --work-dir "$WORK" \
  --top-k 1000 --k1 0.9 --b 0.4 \
  --tag joho-bm25-marco | tee "$WORK/scorecard.txt"

echo "==> [5/6] filtering corpus to the union of every query's top-$DEPTH candidates"
# TREC run is space-separated: <qid> Q0 <docid> <rank> <score> <tag>  -> rank is $4, docid $3.
awk -v d="$DEPTH" '$4 <= d {print $3}' "$WORK/run.txt" | sort -u > "$WORK/top_docids.txt"
# corpus.tsv is TAB-separated: <docid>\t<text>. Keep only rows whose docid we need.
awk -F'\t' 'NR==FNR{keep[$1]=1; next} ($1 in keep)' \
    "$WORK/top_docids.txt" "$WORK/corpus.tsv" > "$WORK/corpus_top${DEPTH}.tsv"
echo "    $(wc -l < "$WORK/top_docids.txt") unique docids; "\
"$(wc -l < "$WORK/corpus_top${DEPTH}.tsv") passages in the subset"
ls -lh "$WORK/run.txt" "$WORK/corpus_top${DEPTH}.tsv" "$WORK/queries.tsv" | sed 's/^/    /'

echo "==> [6/6] uploading SMALL artifacts to s3://$BUCKET/$DEST (NOT the 2.6 GB corpus)"
# Explicit per-file cp (not a dir sync) so we never accidentally push the full corpus,
# and one transient error can't abort the rest of the uploads.
for f in scorecard.txt queries.tsv "corpus_top${DEPTH}.tsv" run.txt; do
  echo "    -> $f"
  aws s3 cp "$WORK/$f" "s3://$BUCKET/$DEST/$f" --only-show-errors || \
    aws s3 cp "$WORK/$f" "s3://$BUCKET/$DEST/$f" --only-show-errors   # one retry
done

echo "Done. BM25 scorecard:"
cat "$WORK/scorecard.txt"
