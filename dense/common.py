"""Tiny shared helpers for the dense-retrieval scripts.

Two formats matter here:
  * the engine's TSV  (<id>\\t<text>) — same files the C++ engine + eval use
  * the TREC run file (<qid> Q0 <docid> <rank> <score> <tag>) — what evaluate.py grades

Keeping these in one place means embed.py / dense_search.py / fuse_rrf.py all read
and write the exact same on-disk shapes the rest of the project already speaks.
"""
from __future__ import annotations

from pathlib import Path


def pick_device() -> str:
    """Prefer Apple Silicon GPU (Metal / MPS), then CUDA, then CPU."""
    import torch  # imported lazily so non-embedding scripts don't need torch

    if torch.backends.mps.is_available():
        return "mps"
    if torch.cuda.is_available():
        return "cuda"
    return "cpu"


def read_tsv(path: str | Path) -> tuple[list[str], list[str]]:
    """Read an <id>\\t<text> file. Splits on the FIRST tab (text may contain spaces).

    Returns parallel lists (ids, texts) in file order — row i of the embedding
    matrix will correspond to ids[i].
    """
    ids: list[str] = []
    texts: list[str] = []
    with open(path, encoding="utf-8") as f:
        for line in f:
            line = line.rstrip("\n")
            if not line:
                continue
            tab = line.find("\t")
            if tab < 0:
                continue
            ids.append(line[:tab])
            texts.append(line[tab + 1:])
    return ids, texts


def write_trec_run(path: str | Path, per_query, tag: str) -> int:
    """Write a TREC run. `per_query` is an iterable of (qid, [(docid, score), ...])
    where each doc list is ALREADY sorted best-first. Returns the line count."""
    n = 0
    with open(path, "w", encoding="utf-8") as f:
        for qid, hits in per_query:
            for rank, (docid, score) in enumerate(hits, start=1):
                f.write(f"{qid} Q0 {docid} {rank} {score:.6f} {tag}\n")
                n += 1
    return n


def read_trec_run(path: str | Path) -> dict[str, list[tuple[str, float]]]:
    """Read a TREC run into {qid: [(docid, score), ...]} sorted best-first.

    We re-sort by score rather than trusting the rank column, so fusing runs from
    different tools (our C++ engine, faiss, …) is robust to formatting quirks.
    """
    runs: dict[str, list[tuple[str, float]]] = {}
    with open(path, encoding="utf-8") as f:
        for line in f:
            parts = line.split()
            if len(parts) < 6:
                continue
            qid, _q0, docid, _rank, score, _tag = parts[:6]
            runs.setdefault(qid, []).append((docid, float(score)))
    for qid in runs:
        runs[qid].sort(key=lambda x: x[1], reverse=True)
    return runs
