# Joho — a from-scratch search engine

> A modern search engine built to understand how large-scale search works end to end:
> a fast C++ retrieval core, an ML "meaning" layer in Python, rigorous relevance
> measurement, and a Next.js (React) UI — all measured against real labeled data.

The goal is not just "it returns results" — it is **measurable** result quality, with every
layer built from scratch and judged by standard IR metrics.

---

## The one-paragraph version (plain English)

A search engine is a giant **back-of-the-book index**. Instead of re-reading every
document each time you ask a question, we build an index once: for every word, a list of
which documents contain it. When you search, we look up your words in that index, **score**
each matching document to find the best ones, and show them ranked. Modern engines add a
second trick — turning text into numbers that capture **meaning** — so "car" can also find
"automobile." This project builds both, combines them, and **measures** how good the
results are using datasets that come with a human answer key.

## The pipeline

```
 Corpus (MS MARCO / BEIR — comes with an answer key)
        │
        ▼
 Parse + tokenize  ──►  C++ ENGINE: inverted index + BM25  ─┐
        │                                                    │  (word matching)
        ▼                                                    │
 Embeddings (Python) ──► dense vector index (meaning) ───────┤
                                                             ▼
                                              Hybrid fusion (combine both lists)
                                                             ▼
                                              Re-ranker (cross-encoder, top ~50)
                                                             ▼
        Autocomplete + spell-fix ──►  API  ──►  React UI
                                                             ▼
                                  EVALUATION: grade results vs. the answer key
```

## Repository layout

```
engine/   C++  — tokenizer, inverted index, BM25, trie + SymSpell, sharding, gRPC server
dense/    Py   — embeddings (BGE-small), HNSW, RRF fusion, cross-encoder re-ranker
eval/     Py   — grading harness: nDCG@10, MRR@10, Recall@k  (dataset → run → scorecard)
gateway/  Py   — FastAPI gateway that orchestrates the funnel over gRPC
web/      TS   — Next.js search UI with a "why this result?" score panel
proto/         — joho.proto: the engine↔gateway gRPC contract
deploy/        — Dockerfiles + GCP scripts (Cloud Run, GCS, Spot-VM jobs, budget alerts)
scripts/       — codegen helpers (gen_proto.sh)
data/          — downloaded corpora + built indexes (git-ignored)
```

## Status & headline numbers

P1–P7 are implemented and validated locally; P8 packages the stack as containers for a
scale-to-zero Cloud Run deployment (idle cost ≈ $0), with GCS storage and ephemeral Spot-VM
batch jobs.

| Phase | What | Result |
|---|---|---|
| P1 | C++ BM25 + eval harness | SciFact nDCG@10 **0.661** (≈ published 0.665); MS MARCO 8.8M MRR@10 **0.182** (≈ Anserini 0.184) |
| P1+ | Query-path optimization | profiled at 8.8M docs → dense accumulator + parallelism = **19.8×** (1616 → 81 ms/q), byte-identical |
| P1++ | WAND dynamic pruning | full 8.8M MS MARCO, *on top of* the optimized scan: **3.0×** faster at top-10 (87.4 → 29.2 ms/q), **identical** MRR@10 (0.1821); win tapers to 2.1× (k=100) and 1.3× (k=1000) as expected |
| P2 | Compression + mmap | postings **~3.7×** smaller; mmap backend byte-identical, O(1) load |
| P3 | Dense + hybrid (RRF) | SciFact dense **0.713**; NFCorpus hybrid **0.356** beats both |
| P4 | Cross-encoder re-rank | MS MARCO MRR@10 **0.182 → 0.356** (~2×), recall flat (hybrid EC2+M4 run) |
| P5 | Autocomplete + spell | weighted-trie best-first top-k; SymSpell `pateint→patient` |
| P6 | gRPC gateway + Next.js UI | browser query → ranked results + per-stage "why?" panel |
| P7 | Sharding (scatter-gather) | 4 shards vs single = **−0.0028 nDCG@10** (local-stats cost) |
| P8 | GCP (Cloud Run, scale-to-zero) | containerized; one-command deploy (see `deploy/`) |

## Quickstart (local)

```bash
# 1. Build the C++ engine
cmake -S engine -B engine/build -DCMAKE_BUILD_TYPE=Release && cmake --build engine/build -j

# 2. Reproduce the BM25 baseline on a BEIR set (exports data, runs engine, grades it)
bash eval/setup_env.sh
eval/.venv/bin/python eval/run_eval.py --dataset beir/scifact/test

# 3. (optional) Autocomplete / spell-check REPL straight off the corpus
engine/build/joho_suggest --corpus data/beir_scifact_test/corpus.tsv

# 4. (optional) Full stack in Docker (engine + gateway), or deploy: see deploy/README.md
docker compose up
```

## Design notes

Key components and their rationale:

- **C++ retrieval core** — inverted index with delta+varint posting compression and an
  `mmap`-backed on-disk reader behind a single `IndexReader` interface, so BM25 scores RAM
  or disk identically.
- **WAND dynamic pruning** — document-at-a-time retrieval that uses per-term max-score upper
  bounds to skip documents that provably cannot enter the top-k. Verified lossless against an
  exhaustive scan on the full 8.8M-passage corpus (identical MRR@10/nDCG@10), with the speedup
  growing as k shrinks — the expected behaviour, measured rather than assumed.
- **Hybrid retrieval funnel** — BM25 (lexical) + dense embeddings (BGE-small, FAISS HNSW)
  fused with Reciprocal Rank Fusion, then a cross-encoder reranker on the top candidates.
- **Autocomplete & spell-correct** — a weighted trie with best-first (branch-and-bound)
  top-k completion, plus SymSpell with a Damerau-OSA re-check.
- **Scatter-gather sharding** — hash-partitioned shards searched in parallel, merged by
  score; the local-vs-global BM25 statistics trade-off is measured, not assumed.
- **Serving** — a gRPC C++ engine, a FastAPI gateway running the funnel, and a Next.js UI
  with a per-result score breakdown. The gateway degrades to lexical-only if the ML layer
  is unavailable.

## License

MIT
