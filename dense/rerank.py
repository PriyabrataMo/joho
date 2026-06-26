"""Cross-encoder re-ranking of a first-stage TREC run (P4 — the funnel's precision tip).

A bi-encoder (P3) embeds query and doc *separately*, so scoring is a cheap dot product
but the model never sees the pair together. A **cross-encoder** feeds `[query, doc]` as
one input through a transformer and emits a single relevance score — attention runs
across both texts jointly, so it judges relevance far more sharply. The price: no
precomputation, one model forward-pass per (query, doc) pair. So we run it only on the
top-k candidates a cheap retriever already produced.

What this script does:
  * read a first-stage run (BM25 / dense / hybrid) and the query + corpus text
  * for each query, take its top --rerank-depth candidates and score (query, doc) pairs
  * reorder ONLY those top-k by cross-encoder score; leave the deeper tail untouched

Because we only reorder within the top-k, Recall@k and Recall@1000 are unchanged vs the
first stage — the lift shows up in nDCG@10 / RR@10 (the top of the list). The candidate
*set* is the recall ceiling the first stage already set; the re-ranker just fixes the order.

Run:
  python rerank.py --run ../data/beir_scifact_test/run_hybrid.txt \\
                   --queries ../data/beir_scifact_test/queries.tsv \\
                   --corpus  ../data/beir_scifact_test/corpus.tsv \\
                   --output  ../data/beir_scifact_test/run_hybrid_ce.txt \\
                   --rerank-depth 100 --tag joho-ce
"""
from __future__ import annotations

import argparse
import time
from pathlib import Path

from common import pick_device, read_tsv, read_trec_run, write_trec_run


def rerank(run_path, queries_path, corpus_path, model_name, depth, batch_size, max_length):
    run = read_trec_run(run_path)                       # {qid: [(docid, score), ...]} best-first
    q_ids, q_texts = read_tsv(queries_path)
    d_ids, d_texts = read_tsv(corpus_path)
    qtext = dict(zip(q_ids, q_texts))
    dtext = dict(zip(d_ids, d_texts))

    # Build ONE flat list of (query, doc) pairs across all queries, so the model runs in
    # big batches (good MPS utilization). `spans[qid] = (start, end)` slices the scores back.
    pairs, order, spans = [], [], {}
    for qid in sorted(run.keys()):
        if qid not in qtext:
            continue
        cands = run[qid][:depth]
        start = len(pairs)
        for docid, _ in cands:
            if docid in dtext:
                pairs.append((qtext[qid], dtext[docid]))
                order.append(docid)
        spans[qid] = (start, len(pairs))

    from sentence_transformers import CrossEncoder  # heavy import, do it late

    device = pick_device()
    print(f"Loading cross-encoder '{model_name}' on '{device}'; scoring {len(pairs)} pairs "
          f"(max_length={max_length})")
    # Truncating long docs to ~256–320 tokens barely moves re-rank quality (the relevance
    # signal is concentrated early) but cuts the per-pair forward cost a lot — important on MPS.
    model = CrossEncoder(model_name, device=device, max_length=max_length)
    t0 = time.perf_counter()
    scores = model.predict(pairs, batch_size=batch_size, show_progress_bar=True)
    elapsed = time.perf_counter() - t0
    print(f"  scored {len(pairs)} pairs in {elapsed:.2f}s "
          f"({len(pairs)/elapsed:.0f} pairs/s, {elapsed*1000/max(1,len(spans)):.1f} ms/query)")

    # Reassemble each query's run: top-k reordered by CE score, then the untouched tail
    # (ranks > depth) kept in first-stage order, scored strictly below the CE block so the
    # global ordering is preserved when evaluate.py re-sorts by score.
    fused = []
    for qid in sorted(run.keys()):
        if qid not in spans:
            fused.append((qid, run.get(qid, [])))
            continue
        start, end = spans[qid]
        reranked = sorted(
            ((order[i], float(scores[i])) for i in range(start, end)),
            key=lambda kv: kv[1], reverse=True,
        )
        tail = run[qid][depth:]
        floor = (min(s for _, s in reranked) if reranked else 0.0) - 1.0
        tail_scored = [(docid, floor - rank) for rank, (docid, _) in enumerate(tail)]
        fused.append((qid, reranked + tail_scored))
    return fused


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--run", required=True, help="first-stage TREC run to re-rank")
    ap.add_argument("--queries", required=True, help="queries TSV <qid>\\t<text>")
    ap.add_argument("--corpus", required=True, help="corpus TSV <docid>\\t<text>")
    ap.add_argument("--output", required=True, help="re-ranked TREC run to write")
    ap.add_argument("--model", default="cross-encoder/ms-marco-MiniLM-L-6-v2")
    ap.add_argument("--rerank-depth", type=int, default=100,
                    help="how many top candidates per query to re-score (default 100)")
    ap.add_argument("--batch-size", type=int, default=64)
    ap.add_argument("--max-length", type=int, default=512,
                    help="max tokens per (query,doc) pair; smaller = faster (default 512)")
    ap.add_argument("--tag", default="joho-ce")
    args = ap.parse_args()

    fused = rerank(Path(args.run), Path(args.queries), Path(args.corpus),
                   args.model, args.rerank_depth, args.batch_size, args.max_length)
    n_lines = write_trec_run(args.output, fused, args.tag)
    print(f"Wrote {n_lines} lines -> {args.output}")


if __name__ == "__main__":
    main()
