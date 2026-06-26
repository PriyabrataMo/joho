"""Export a standard IR dataset to the TSV files the C++ engine eats.

`ir_datasets` gives us, for a dataset id like ``beir/scifact/test`` or
``msmarco-passage/dev/small``:

  * the document corpus      -> we write ``corpus.tsv``  (<doc_id>\\t<text>)
  * the queries              -> we write ``queries.tsv`` (<query_id>\\t<text>)
  * the relevance judgments  -> consumed later by evaluate.py (not written here)

It downloads + caches automatically (under ~/.ir_datasets), so the first run is
slow and later runs are instant.

Run standalone:
    python export_dataset.py --dataset beir/scifact/test --out-dir ../data/scifact
"""
from __future__ import annotations

import argparse
from pathlib import Path

import ir_datasets


def clean(text: str) -> str:
    """Flatten any whitespace (tabs, newlines) to single spaces.

    The C++ loader splits each line on the FIRST tab, so the text field must not
    contain tabs or newlines — otherwise a single document would look like many.
    """
    return " ".join((text or "").split())


def doc_text(doc) -> str:
    """Combine a document's title and body the way BEIR's BM25 baselines do.

    BEIR docs carry a separate ``title``; the standard recipe indexes
    ``title + " " + text``. MS MARCO passages have no title, so we just use body.
    """
    title = (getattr(doc, "title", "") or "").strip()
    body = (getattr(doc, "text", "") or "").strip()
    return f"{title} {body}".strip() if title else body


def export(dataset_id: str, out_dir: Path, docs_dataset: str | None = None,
           max_docs: int | None = None) -> tuple[Path, Path]:
    """Write corpus.tsv + queries.tsv for `dataset_id`. Returns their paths."""
    out_dir = Path(out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)

    ds = ir_datasets.load(dataset_id)

    # Most eval splits (e.g. .../test, .../dev/small) inherit the doc corpus from
    # their parent dataset, so `ds.has_docs()` is usually True. If not, the caller
    # can point --docs-dataset at the parent that actually carries the corpus.
    if docs_dataset:
        docs_ds = ir_datasets.load(docs_dataset)
    elif ds.has_docs():
        docs_ds = ds
    else:
        raise SystemExit(
            f"dataset '{dataset_id}' has no docs; pass --docs-dataset "
            f"(e.g. the parent corpus id)"
        )

    corpus_path = out_dir / "corpus.tsv"
    print(f"Writing corpus -> {corpus_path}")
    n_docs = 0
    with corpus_path.open("w", encoding="utf-8") as f:
        for doc in docs_ds.docs_iter():
            if max_docs is not None and n_docs >= max_docs:
                break
            f.write(f"{doc.doc_id}\t{clean(doc_text(doc))}\n")
            n_docs += 1
            if n_docs % 100000 == 0:
                print(f"  ...{n_docs} docs")
    print(f"  {n_docs} documents")

    queries_path = out_dir / "queries.tsv"
    print(f"Writing queries -> {queries_path}")
    n_q = 0
    with queries_path.open("w", encoding="utf-8") as f:
        for q in ds.queries_iter():
            f.write(f"{q.query_id}\t{clean(q.text)}\n")
            n_q += 1
    print(f"  {n_q} queries")

    return corpus_path, queries_path


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--dataset", required=True,
                    help="ir_datasets id with queries+qrels, e.g. beir/scifact/test")
    ap.add_argument("--out-dir", required=True, help="where to write the TSV files")
    ap.add_argument("--docs-dataset", default=None,
                    help="override corpus source (e.g. msmarco-passage for the dev split)")
    ap.add_argument("--max-docs", type=int, default=None,
                    help="cap the corpus size (for quick local experiments)")
    args = ap.parse_args()
    export(args.dataset, Path(args.out_dir), args.docs_dataset, args.max_docs)


if __name__ == "__main__":
    main()
