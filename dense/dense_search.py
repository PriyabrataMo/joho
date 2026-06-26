"""Nearest-neighbor retrieval over passage embeddings -> a TREC run file.

Given query vectors and passage vectors (both L2-normalized, so inner product ==
cosine similarity), find each query's top-k closest passages. Two backends:

  --index-type exact   brute force: one matrix multiply Q·Dᵀ, then top-k per row.
                       This is the GROUND TRUTH — every doc is compared, no
                       approximation. Trivial at a few thousand docs.
  --index-type hnsw    faiss HNSW graph index: approximate nearest neighbors.
                       What you reach for at millions of vectors — sub-linear search,
                       at the cost of occasionally missing a true neighbor. We measure
                       that miss rate (recall vs exact) so the trade-off is a number.

Output is a standard TREC run, graded by ../eval/evaluate.py exactly like the BM25 run.

Run:
  python dense_search.py --doc-emb  ../data/beir_scifact_test/corpus_bge \\
                         --query-emb ../data/beir_scifact_test/queries_bge \\
                         --output ../data/beir_scifact_test/run_dense.txt \\
                         --index-type exact --top-k 1000 --tag joho-dense
"""
from __future__ import annotations

import argparse
import time
from pathlib import Path

import numpy as np

from common import write_trec_run


def load_emb(prefix: Path) -> tuple[np.ndarray, list[str]]:
    prefix = Path(prefix)
    emb = np.load(prefix.with_suffix(".npy")).astype(np.float32)
    with prefix.with_suffix(".ids.txt").open(encoding="utf-8") as f:
        ids = [line.rstrip("\n") for line in f if line.strip()]
    if len(ids) != emb.shape[0]:
        raise SystemExit(f"id/embedding mismatch for {prefix}: {len(ids)} ids vs {emb.shape[0]} rows")
    return emb, ids


def search_exact(doc_emb, doc_ids, q_emb, top_k):
    """Brute-force cosine KNN. Returns list of (qid_row, [(docid, score), ...])."""
    k = min(top_k, doc_emb.shape[0])
    # One big GEMM: sims[i, j] = cosine(query_i, doc_j). Float32, fits easily here.
    sims = q_emb @ doc_emb.T                       # [Nq, Nd]
    # argpartition pulls the top-k unsorted in O(Nd); then we sort just those k.
    part = np.argpartition(-sims, k - 1, axis=1)[:, :k]
    out = []
    for i in range(sims.shape[0]):
        cols = part[i]
        order = cols[np.argsort(-sims[i, cols])]   # sort the k by score desc
        out.append([(doc_ids[j], float(sims[i, j])) for j in order])
    return out


def search_hnsw(doc_emb, doc_ids, q_emb, top_k, m, ef_construction, ef_search):
    """Approximate KNN with a faiss HNSW graph (inner-product metric)."""
    import faiss

    dim = doc_emb.shape[1]
    index = faiss.IndexHNSWFlat(dim, m, faiss.METRIC_INNER_PRODUCT)
    index.hnsw.efConstruction = ef_construction
    t0 = time.perf_counter()
    index.add(doc_emb)                             # builds the navigable-small-world graph
    build_s = time.perf_counter() - t0

    index.hnsw.efSearch = ef_search
    k = min(top_k, doc_emb.shape[0])
    t0 = time.perf_counter()
    scores, idxs = index.search(q_emb, k)          # [Nq, k] similarities + doc rows
    search_s = time.perf_counter() - t0
    print(f"  HNSW build {build_s:.2f}s (M={m}, efC={ef_construction}); "
          f"search {search_s*1000/q_emb.shape[0]:.3f} ms/query (efSearch={ef_search})")

    out = []
    for i in range(idxs.shape[0]):
        hits = [(doc_ids[j], float(scores[i, c]))
                for c, j in enumerate(idxs[i]) if j != -1]
        out.append(hits)
    return out


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--doc-emb", required=True, help="passage embedding prefix")
    ap.add_argument("--query-emb", required=True, help="query embedding prefix")
    ap.add_argument("--output", required=True, help="TREC run file to write")
    ap.add_argument("--index-type", choices=["exact", "hnsw"], default="exact")
    ap.add_argument("--top-k", type=int, default=1000)
    ap.add_argument("--tag", default="joho-dense")
    ap.add_argument("--hnsw-m", type=int, default=32, help="graph degree (links/node)")
    ap.add_argument("--hnsw-ef-construction", type=int, default=200)
    ap.add_argument("--hnsw-ef-search", type=int, default=128)
    args = ap.parse_args()

    doc_emb, doc_ids = load_emb(args.doc_emb)
    q_emb, q_ids = load_emb(args.query_emb)
    print(f"docs {doc_emb.shape}  queries {q_emb.shape}  index={args.index_type}")

    t0 = time.perf_counter()
    if args.index_type == "exact":
        per_q = search_exact(doc_emb, doc_ids, q_emb, args.top_k)
    else:
        per_q = search_hnsw(doc_emb, doc_ids, q_emb, args.top_k,
                            args.hnsw_m, args.hnsw_ef_construction, args.hnsw_ef_search)
    elapsed = time.perf_counter() - t0

    per_query = zip(q_ids, per_q)
    n_lines = write_trec_run(args.output, per_query, args.tag)
    print(f"Searched {len(q_ids)} queries in {elapsed:.3f}s "
          f"({elapsed*1000/len(q_ids):.3f} ms/query); wrote {n_lines} lines -> {args.output}")


if __name__ == "__main__":
    main()
