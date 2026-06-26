# `dense/` — semantic retrieval + hybrid fusion (P3)

Adds *meaning-based* search on top of the C++ BM25 engine. BM25 matches words; a
small embedding model matches **meaning** (so "heart attack" finds "myocardial
infarction"). Then we fuse the two ranked lists. Everything here speaks the same
on-disk formats as the rest of the project — TSV in, **TREC run files** out — so
`../eval/evaluate.py` grades these runs exactly like the BM25 run.

## Setup (separate venv — keeps the eval venv lean)

```bash
bash dense/setup_env.sh        # torch + sentence-transformers + faiss, from public PyPI
```

Verifies the Apple Silicon GPU (MPS) is available at the end. ~Hundreds of MB (torch); one-time.

## Pipeline

All scripts import `common.py`, so run them with `PYTHONPATH=dense` (or from `dense/`).
Example on SciFact (corpus/queries already exported by `eval/`):

```bash
D=$PWD; export PYTHONPATH=$D/dense
DS=$D/data/beir_scifact_test
PY=$D/dense/.venv/bin/python

# 1. embed corpus (passages) and queries → <prefix>.npy + <prefix>.ids.txt
$PY dense/embed.py --input $DS/corpus.tsv  --output $DS/corpus_bge  --kind passage
$PY dense/embed.py --input $DS/queries.tsv --output $DS/queries_bge --kind query

# 2. dense retrieval (exact ground truth, or approximate HNSW) → TREC run
$PY dense/dense_search.py --doc-emb $DS/corpus_bge --query-emb $DS/queries_bge \
    --output $DS/run_dense.txt --index-type exact --top-k 1000
#   ...or --index-type hnsw --hnsw-ef-search 128

# 3. hybrid: fuse the BM25 run (from joho_batch) with the dense run
$PY dense/fuse_rrf.py --runs $DS/run.txt $DS/run_dense.txt \
    --output $DS/run_hybrid.txt --k 60 --top-k 1000

# 4. grade any run with the shared evaluator
$D/eval/.venv/bin/python eval/evaluate.py --run $DS/run_hybrid.txt --dataset beir/scifact/test
```

## Files
- `common.py` — TSV reader, TREC run read/write, MPS/CPU device picker.
- `embed.py` — text → L2-normalized float32 vectors (BGE-small via MPS). `--kind`
  adds the BGE query instruction to queries only.
- `dense_search.py` — `--index-type exact` (brute-force cosine, the ground truth) or
  `hnsw` (faiss approximate NN); writes a TREC run.
- `fuse_rrf.py` — Reciprocal Rank Fusion of two+ runs (rank-based, no score calibration).

## Results
- Dense (BGE-small) beats BM25 on both sets: SciFact 0.713 vs 0.661, NFCorpus 0.343 vs 0.305 nDCG@10.
- Hybrid (RRF) **beats both** on NFCorpus (0.356) where the signals are complementary; on
  SciFact (dense dominant) it lands between them but gives the best Recall@100.
- HNSW @ efSearch=128 matches exact top-10 (recall@10 = 1.0) at ~6× faster search.
