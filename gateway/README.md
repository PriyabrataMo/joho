# `gateway/` — the funnel orchestrator (P6)

A FastAPI service that turns a query into ranked, **explainable** results. It owns the
wiring of the whole retrieval funnel; each stage is independent and degrades gracefully.

```
browser ──REST/JSON──▶  gateway  ──gRPC──▶  engine (C++: BM25 + trie + SymSpell)
                          │
                          ├─ bi-encoder (BGE)  → dense candidates
                          ├─ RRF               → fuse lexical + dense
                          └─ cross-encoder     → re-rank the fused top-k
```

The gateway speaks gRPC to the C++ engine for the lexical + query-assist layer (it
never re-implements BM25), and runs the ML stages (embedding, fusion, re-rank) itself.

## Endpoints
| Method | Path | What |
|---|---|---|
| GET | `/healthz` | gateway + engine health, which stages are live |
| GET | `/autocomplete?q=&k=` | prefix completions (engine trie) |
| GET | `/correct?q=` | "did you mean?" (engine SymSpell) |
| GET | `/search?q=&k=&autocorrect=` | the full funnel; each result carries its per-stage scores |

## Setup & run
```bash
bash gateway/setup_env.sh          # venv + deps (public PyPI) + generate gRPC stubs

# 1) start the engine (the gRPC server). Needs joho_server built (brew install grpc protobuf).
engine/build/joho_server --corpus data/beir_scifact_test/corpus.tsv --port 50051

# 2) start the gateway
cd gateway && .venv/bin/uvicorn app:app --host 0.0.0.0 --port 8000
#    open http://localhost:8000/docs for the live OpenAPI console
```

## Configuration (env vars — see `config.py`)
| Var | Default | Meaning |
|---|---|---|
| `JOHO_ENGINE_ADDR` | `localhost:50051` | the C++ engine's gRPC address |
| `JOHO_DATASET_DIR` | `data/beir_scifact_test` | corpus + precomputed doc embeddings |
| `JOHO_ENABLE_DENSE` | `1` | bi-encoder + fusion on/off |
| `JOHO_ENABLE_RERANK` | `1` | cross-encoder re-rank on/off |
| `JOHO_ENABLE_SPELLCHECK` | `1` | auto-correct the query before searching |
| `JOHO_BM25_DEPTH` / `JOHO_DENSE_DEPTH` | `200` | candidates per first stage |
| `JOHO_RERANK_DEPTH` | `50` | cross-encoder budget |
| `JOHO_EMBED_MODEL` | `BAAI/bge-small-en-v1.5` | must match the doc embeddings |
| `JOHO_RERANK_MODEL` | `BAAI/bge-reranker-base` | the cross-encoder |

> The dense stage needs precomputed document embeddings at
> `$JOHO_DATASET_DIR/corpus_bge.npy` + `.ids.txt` (produced by `dense/embed.py`). If
> they're missing or `JOHO_ENABLE_DENSE=0`, the gateway serves lexical-only and still
> works — the response's `pipeline` block reports which stages actually ran.
