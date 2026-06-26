"""One command: dataset -> export -> C++ BM25 -> grade -> scorecard.

This ties the whole P1 loop together so you can reproduce a baseline with a
single invocation:

    python run_eval.py --dataset beir/scifact/test
    python run_eval.py --dataset msmarco-passage/dev/small --docs-dataset msmarco-passage

It (1) exports the corpus+queries to TSV, (2) runs the C++ `joho_batch` engine to
produce a TREC run file, (3) grades it with ir_measures, and prints the metrics.
"""
from __future__ import annotations

import argparse
import subprocess
import sys
from pathlib import Path

import export_dataset
import evaluate

# eval/ -> repo root -> engine/build/joho_batch
DEFAULT_ENGINE = Path(__file__).resolve().parent.parent / "engine" / "build" / "joho_batch"


def slug(dataset_id: str) -> str:
    """Turn 'beir/scifact/test' into 'beir_scifact_test' for a tidy work dir."""
    return dataset_id.replace("/", "_")


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--dataset", required=True,
                    help="ir_datasets id with queries+qrels, e.g. beir/scifact/test")
    ap.add_argument("--docs-dataset", default=None,
                    help="override corpus source (e.g. msmarco-passage for dev/small)")
    ap.add_argument("--engine", default=str(DEFAULT_ENGINE),
                    help="path to the joho_batch binary")
    ap.add_argument("--work-dir", default=None,
                    help="where to put TSVs + run file (default: ../data/<dataset>)")
    ap.add_argument("--top-k", type=int, default=1000)
    ap.add_argument("--k1", type=float, default=0.9)
    ap.add_argument("--b", type=float, default=0.4)
    ap.add_argument("--tag", default="joho-bm25")
    ap.add_argument("--max-docs", type=int, default=None,
                    help="cap corpus size for quick experiments")
    ap.add_argument("--skip-export", action="store_true",
                    help="reuse existing corpus.tsv/queries.tsv in the work dir")
    args = ap.parse_args()

    engine = Path(args.engine)
    if not engine.exists():
        sys.exit(f"engine binary not found: {engine}\n"
                 f"build it first:  cmake --build engine/build")

    work_dir = Path(args.work_dir) if args.work_dir else \
        Path(__file__).resolve().parent.parent / "data" / slug(args.dataset)
    work_dir.mkdir(parents=True, exist_ok=True)

    corpus = work_dir / "corpus.tsv"
    queries = work_dir / "queries.tsv"
    run_path = work_dir / "run.txt"

    # 1. Export corpus + queries to TSV (unless reusing a previous export).
    if args.skip_export and corpus.exists() and queries.exists():
        print(f"Reusing existing TSVs in {work_dir}")
    else:
        corpus, queries = export_dataset.export(
            args.dataset, work_dir, args.docs_dataset, args.max_docs)

    # 2. Run the C++ engine to produce a TREC run file.
    cmd = [str(engine),
           "--corpus", str(corpus),
           "--queries", str(queries),
           "--output", str(run_path),
           "--top-k", str(args.top_k),
           "--k1", str(args.k1),
           "--b", str(args.b),
           "--tag", args.tag]
    print(f"\nRunning engine:\n  {' '.join(cmd)}")
    subprocess.run(cmd, check=True)

    # 3. Grade the run and print the scorecard.
    results = evaluate.evaluate(run_path, args.dataset)
    evaluate.print_results(args.dataset, results)


if __name__ == "__main__":
    main()
