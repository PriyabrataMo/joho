"""Grade a TREC run file against a dataset's relevance judgments (qrels).

This is where a list of retrieved documents becomes a *number you can quote*:
nDCG@10, MRR@10 (= RR@10), Recall@100/@1000, MAP. We use `ir_measures` so these
match the definitions behind published baselines.

Run standalone:
    python evaluate.py --run ../data/scifact/run.txt --dataset beir/scifact/test
"""
from __future__ import annotations

import argparse
from pathlib import Path

import ir_datasets
import ir_measures
from ir_measures import nDCG, RR, R, AP

# The default scorecard. @k means "judged on the top k results".
#   nDCG@10  : graded relevance, position-discounted (the BEIR headline metric)
#   RR@10    : reciprocal rank of the first relevant hit = MS MARCO's MRR@10
#   R@100/1k : recall — did we even retrieve the relevant docs within depth k?
#   AP       : mean average precision over all relevant docs
DEFAULT_MEASURES = [nDCG @ 10, RR @ 10, R @ 100, R @ 1000, AP]


def parse_measure(spec: str):
    """Parse a measure string like 'nDCG@10', 'R@100', or 'AP'.

    We deliberately do NOT use ``ir_measures.parse_measure`` here: its string parser
    relies on ``ast.Num``, which was deprecated in Python 3.8 and *removed* in 3.12,
    so it raises ``AttributeError`` on our Python 3.14. Building the measure from the
    registry via the ``@`` operator (the same thing DEFAULT_MEASURES does) is both
    version-proof and exactly equivalent.
    """
    spec = spec.strip()
    if "@" in spec:
        name, cutoff = spec.split("@", 1)
        return getattr(ir_measures, name.strip()) @ int(cutoff)
    return getattr(ir_measures, spec)


def evaluate(run_path: Path, dataset_id: str, measures=None) -> dict:
    """Return {measure: score} for the run against the dataset's qrels."""
    measures = measures or DEFAULT_MEASURES
    ds = ir_datasets.load(dataset_id)
    if not ds.has_qrels():
        raise SystemExit(f"dataset '{dataset_id}' has no qrels to grade against")

    qrels = list(ds.qrels_iter())          # human relevance judgments
    run = ir_measures.read_trec_run(str(run_path))  # our engine's output
    return ir_measures.calc_aggregate(measures, qrels, run)


def print_results(dataset_id: str, results: dict) -> None:
    print(f"\nResults on {dataset_id}")
    print("  " + "-" * 28)
    for measure in sorted(results, key=str):
        print(f"  {str(measure):<12} {results[measure]:.4f}")
    print()


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--run", required=True, help="TREC run file to grade")
    ap.add_argument("--dataset", required=True,
                    help="ir_datasets id providing the qrels, e.g. beir/scifact/test")
    ap.add_argument("--measures", default=None,
                    help="space-separated measures (e.g. 'nDCG@10 RR@10'); "
                         "defaults to the standard scorecard")
    args = ap.parse_args()

    measures = None
    if args.measures:
        measures = [parse_measure(m) for m in args.measures.split()]

    results = evaluate(Path(args.run), args.dataset, measures)
    print_results(args.dataset, results)


if __name__ == "__main__":
    main()
