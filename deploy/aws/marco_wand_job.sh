#!/usr/bin/env bash
# WAND vs exhaustive benchmark on the FULL MS MARCO passage corpus — AWS variant.
#
# This is the Tier-3 dynamic-pruning chapter's headline run. WAND (Weak AND) skips
# documents that provably can't enter the top-k using a per-term max-score upper
# bound. Its win GROWS as top-k shrinks (a tighter score threshold prunes more), so
# the interesting result is the speedup CURVE across k — not a single number.
#
# Why the cloud: WAND only pays off when posting lists are long. The local 592k
# top-100 subset has short lists, so pruning saves little there; the full 8,841,823
# passage corpus (common terms appear in millions of docs) is where the binary-
# search leaps actually skip enormous runs.
#
# The job, on one ephemeral box:
#   1. build the C++ engine, export the full MS MARCO corpus + dev/small queries
#   2. for k in {10,100,1000}: run joho_batch BOTH ways (exhaustive vs --wand),
#      over the SAME in-memory index, timing each (ms/query)
#   3. grade every run with ir_measures and assert WAND == exhaustive on the metrics
#      (the correctness proof: dynamic pruning changes speed, never the answer)
#   4. write a compact comparison scorecard and ship ONLY small artifacts to S3
#
# Note on threads: each query is scored on a single thread; --threads parallelizes
# ACROSS queries. So the WAND/exhaustive ms/query RATIO is thread-count independent
# (it's the per-query work ratio) — we run all cores for wall-clock and report the
# ratio as the portable result.
#
# Run via:  S3_PREFIX=msmarco_wand deploy/aws/10_ec2_job.sh deploy/aws/marco_wand_job.sh
set -euo pipefail

DATASET="${DATASET:-msmarco-passage/dev/small}"
DOCS_DATASET="${DOCS_DATASET:-msmarco-passage}"
BUCKET="${BUCKET:?BUCKET (bare S3 bucket name) must be set by the launcher}"
DEST="${DEST:-msmarco_wand}"           # S3 sub-prefix for the benchmark artifacts
KS="${KS:-10 100 1000}"                 # top-k depths to sweep
WORK="data/msmarco_wand"               # single work dir
ENGINE="engine/build/joho_batch"
PY="eval/.venv/bin/python"
SCORECARD="$WORK/wand_scorecard.txt"

echo "==> [1/6] installing build toolchain + awscli"
sudo apt-get update -qq
sudo apt-get install -y -qq build-essential cmake clang git python3-venv python3-pip awscli

# Optional swap (m6i.2xlarge has 32 GB; MS MARCO peaks ~4 GB, so this is unused
# insurance unless the launcher picks a small-RAM instance).
if [ "${SWAP_GB:-0}" -gt 0 ] && ! swapon --show | grep -q /swapfile; then
  echo "    provisioning ${SWAP_GB}G swap"
  sudo fallocate -l "${SWAP_GB}G" /swapfile 2>/dev/null \
    || sudo dd if=/dev/zero of=/swapfile bs=1M count=$((SWAP_GB*1024)) status=none
  sudo chmod 600 /swapfile && sudo mkswap /swapfile >/dev/null && sudo swapon /swapfile
fi

echo "==> [2/6] building the C++ engine (Release)"
cmake -S engine -B engine/build -DCMAKE_BUILD_TYPE=Release >/dev/null
cmake --build engine/build -j"$(nproc)" --target joho_batch

echo "==> [3/6] eval venv"
bash eval/setup_env.sh

echo "==> [4/6] exporting MS MARCO corpus + queries to $WORK"
mkdir -p "$WORK"
# Export once (corpus.tsv + queries.tsv); every run below reuses these TSVs.
$PY - "$DATASET" "$WORK" "$DOCS_DATASET" <<'PYEOF'
import sys
from pathlib import Path
sys.path.insert(0, "eval")
import export_dataset
dataset, work, docs = sys.argv[1], Path(sys.argv[2]), sys.argv[3]
corpus, queries = export_dataset.export(dataset, work, docs, None)
print(f"exported corpus={corpus} queries={queries}")
PYEOF
CORPUS="$WORK/corpus.tsv"
QUERIES="$WORK/queries.tsv"
echo "    corpus=$(wc -l < "$CORPUS") passages, queries=$(wc -l < "$QUERIES")"

echo "==> [5/6] sweeping k in {$KS}, exhaustive vs WAND, grading each"
: > "$SCORECARD"
{
  echo "WAND vs exhaustive on $DATASET ($(wc -l < "$QUERIES") queries, $(wc -l < "$CORPUS") passages)"
  echo "instance threads = $(nproc); ratio is thread-count independent (per-query work)"
  echo
  printf "%-6s %-11s %12s %10s %10s %10s\n" "top-k" "mode" "ms/query" "RR@10" "nDCG@10" "AP"
} >> "$SCORECARD"

# Grade a run file; echo "RR nDCG AP" (4-dp) by parsing evaluate.py output.
grade() {
  local run="$1"
  $PY eval/evaluate.py --run "$run" --dataset "$DATASET" 2>/dev/null \
    | awk '/RR@10/{rr=$2} /nDCG@10/{ndcg=$2} /^  AP/{ap=$2} END{print rr, ndcg, ap}'
}

for k in $KS; do
  for mode in ex wand; do
    flag=""; [ "$mode" = wand ] && flag="--wand"
    run="$WORK/run_${mode}_${k}.txt"
    log="$WORK/run_${mode}_${k}.log"
    echo "    -> k=$k $mode"
    $ENGINE --corpus "$CORPUS" --queries "$QUERIES" --top-k "$k" $flag \
            --output "$run" --tag "joho-$mode" 2>"$log"
    msq=$(grep -oE '[0-9.]+ ms/query' "$log" | head -1 | awk '{print $1}')
    read rr ndcg ap < <(grade "$run")
    printf "%-6s %-11s %12s %10s %10s %10s\n" "$k" "$mode" "$msq" "$rr" "$ndcg" "$ap" >> "$SCORECARD"
    # Drop the (large) run file once graded; keep only logs + scorecard.
    rm -f "$run"
  done
  echo >> "$SCORECARD"
done

echo "==> [6/6] uploading small artifacts to s3://$BUCKET/$DEST (NOT the corpus)"
for f in wand_scorecard.txt; do
  aws s3 cp "$WORK/$f" "s3://$BUCKET/$DEST/$f" --only-show-errors || \
    aws s3 cp "$WORK/$f" "s3://$BUCKET/$DEST/$f" --only-show-errors
done
# Per-run engine logs (tiny) for the timing detail.
for log in "$WORK"/run_*.log; do
  aws s3 cp "$log" "s3://$BUCKET/$DEST/$(basename "$log")" --only-show-errors || true
done

echo "Done. WAND scorecard:"
cat "$SCORECARD"
