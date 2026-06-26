"""Embed a TSV (<id>\\t<text>) into L2-normalized float32 vectors.

This is the one place we lean on a pretrained model: a small sentence-transformer
turns text into a ~384-dim vector where *semantic* similarity ≈ geometric closeness.
We don't train it — we run it forward on the local GPU (PyTorch MPS / CUDA / CPU) to
embed the corpus once and each query.

Why BGE-small-en-v1.5 (the default):
  * 384-dim, ~33M params, ~130MB — fits the laptop, fast on MPS.
  * Strong BEIR zero-shot retrieval scores for its size.
  * Asymmetric (query≠passage): BGE wants a short INSTRUCTION prepended to queries
    only; passages are embedded verbatim. We handle that via --kind.
Alternatives: all-MiniLM-L6-v2 (faster, weaker),
intfloat/e5-small-v2 ("query:"/"passage:" prefixes), bge-base (bigger, better).

Output (two files sharing one prefix, so row i <-> ids[i]):
  <prefix>.npy        float32 [N, dim], each row L2-normalized (so dot == cosine)
  <prefix>.ids.txt    N lines, the external id for each row, in order

Run:
  python embed.py --input ../data/beir_scifact_test/corpus.tsv \\
                  --output ../data/beir_scifact_test/corpus_bge --kind passage
  python embed.py --input ../data/beir_scifact_test/queries.tsv \\
                  --output ../data/beir_scifact_test/queries_bge --kind query
"""
from __future__ import annotations

import argparse
import time
from pathlib import Path

import numpy as np

from common import pick_device, read_tsv

# BGE-v1.5 retrieval recipe: prepend this to QUERIES only (passages stay raw).
BGE_QUERY_INSTRUCTION = "Represent this sentence for searching relevant passages: "


def embed(input_path: Path, output_prefix: Path, kind: str, model_name: str,
          batch_size: int, max_seq_len: int | None) -> None:
    ids, texts = read_tsv(input_path)
    print(f"Read {len(ids)} {kind} rows from {input_path}")

    # Prepend the BGE query instruction for short query->passage retrieval.
    if kind == "query" and "bge" in model_name.lower():
        texts = [BGE_QUERY_INSTRUCTION + t for t in texts]
        print(f"  (prepended BGE query instruction)")

    from sentence_transformers import SentenceTransformer  # heavy import, do it late

    device = pick_device()
    print(f"Loading model '{model_name}' on device '{device}'")
    model = SentenceTransformer(model_name, device=device)
    if max_seq_len:
        model.max_seq_length = max_seq_len

    start = time.perf_counter()
    emb = model.encode(
        texts,
        batch_size=batch_size,
        normalize_embeddings=True,   # unit vectors => inner product IS cosine similarity
        convert_to_numpy=True,
        show_progress_bar=True,
    ).astype(np.float32)
    elapsed = time.perf_counter() - start

    output_prefix = Path(output_prefix)
    output_prefix.parent.mkdir(parents=True, exist_ok=True)
    np.save(output_prefix.with_suffix(".npy"), emb)
    with output_prefix.with_suffix(".ids.txt").open("w", encoding="utf-8") as f:
        f.write("\n".join(ids) + "\n")

    rate = len(ids) / elapsed if elapsed else 0.0
    print(f"Embedded {emb.shape[0]} x {emb.shape[1]} in {elapsed:.2f}s "
          f"({rate:.0f} texts/s) -> {output_prefix.with_suffix('.npy')}")


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--input", required=True, help="TSV <id>\\t<text>")
    ap.add_argument("--output", required=True,
                    help="output prefix; writes <prefix>.npy and <prefix>.ids.txt")
    ap.add_argument("--kind", choices=["passage", "query"], required=True,
                    help="passage = embed verbatim; query = add the model's query prefix")
    ap.add_argument("--model", default="BAAI/bge-small-en-v1.5")
    ap.add_argument("--batch-size", type=int, default=256)
    ap.add_argument("--max-seq-len", type=int, default=None,
                    help="cap tokens per text (smaller = faster, may truncate long docs)")
    args = ap.parse_args()
    embed(Path(args.input), Path(args.output), args.kind, args.model,
          args.batch_size, args.max_seq_len)


if __name__ == "__main__":
    main()
