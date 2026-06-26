"""Reciprocal Rank Fusion (RRF) of two or more TREC runs -> one fused run.

The hybrid idea: BM25 (lexical) and the dense model catch *different* relevant docs
— exact-term matches vs. semantic paraphrases. Fusing their ranked lists usually
beats either alone. The trick is combining them WITHOUT comparing raw scores: BM25
scores are unbounded, cosine sits in [-1, 1], so adding them is meaningless. RRF
sidesteps that by using only each doc's RANK in each list:

    RRF(d) = Σ_runs  1 / (k + rank_run(d))          (rank is 1-based; k = 60)

A doc near the top of either list gets a big contribution; the constant k=60
(Cormack et al., SIGIR 2009) damps the influence of low ranks. No score calibration,
no tuning per dataset — which is exactly why it's the default hybrid baseline.

Run:
  python fuse_rrf.py --runs ../data/beir_scifact_test/run.txt \\
                            ../data/beir_scifact_test/run_dense.txt \\
                     --output ../data/beir_scifact_test/run_hybrid.txt --k 60 --top-k 1000
"""
from __future__ import annotations

import argparse
from pathlib import Path

from common import read_trec_run, write_trec_run


def rrf(runs: list[dict[str, list]], k: int, top_k: int):
    """Fuse pre-sorted runs. Returns [(qid, [(docid, rrf_score), ...]), ...]."""
    all_qids = sorted(set().union(*[set(r.keys()) for r in runs]))
    fused = []
    for qid in all_qids:
        agg: dict[str, float] = {}
        for run in runs:
            for rank, (docid, _score) in enumerate(run.get(qid, []), start=1):
                agg[docid] = agg.get(docid, 0.0) + 1.0 / (k + rank)
        ranked = sorted(agg.items(), key=lambda kv: kv[1], reverse=True)[:top_k]
        fused.append((qid, ranked))
    return fused


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--runs", nargs="+", required=True,
                    help="two or more TREC run files to fuse (e.g. BM25 + dense)")
    ap.add_argument("--output", required=True, help="fused TREC run to write")
    ap.add_argument("--k", type=int, default=60, help="RRF rank constant (default 60)")
    ap.add_argument("--top-k", type=int, default=1000)
    ap.add_argument("--tag", default="joho-hybrid-rrf")
    args = ap.parse_args()

    runs = [read_trec_run(Path(p)) for p in args.runs]
    for path, run in zip(args.runs, runs):
        print(f"loaded {len(run)} queries from {path}")

    fused = rrf(runs, args.k, args.top_k)
    n_lines = write_trec_run(args.output, fused, args.tag)
    print(f"Fused {len(args.runs)} runs (k={args.k}) -> {n_lines} lines in {args.output}")


if __name__ == "__main__":
    main()
