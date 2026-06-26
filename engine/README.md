# engine/ — the C++ retrieval core

The fast, word-based heart of Joho: a tokenizer, an inverted index, and a BM25 scorer.

## Build & run

```bash
cd engine
cmake -S . -B build           # configure (once)
cmake --build build           # compile
./build/joho                  # run the demo queries
./build/joho wild animals that hunt in groups   # search your own query
```

## What's here

| File | What it does | Concept |
|---|---|---|
| `include/joho/tokenizer.hpp` + `src/tokenizer.cpp` | text → lowercase word tokens | tokenize / normalize |
| `include/joho/inverted_index.hpp` + `src/inverted_index.cpp` | term → list of (doc, term-frequency) | inverted index / posting list |
| `include/joho/bm25.hpp` + `src/bm25.cpp` | scores & ranks documents for a query | BM25 / idf |
| `src/main.cpp` | toy demo over 10 built-in documents | — |

## How it works (the 30-second version)

1. **Build time:** every document is tokenized; for each term we record which documents
   contain it and how often (the posting list). We also store each document's length.
2. **Query time:** we tokenize the query, and for each query term we walk its posting list,
   adding a BM25 contribution to every document that contains the term. Then we return the
   highest-scoring documents.

## Next steps
- Feed real data: load MS MARCO passages and reproduce **MRR@10 ≈ 0.187**.
- Persist the index to disk with compression + segments.
- Expose `Search()` over gRPC so the Python gateway can call it.
