"""The retrieval funnel, orchestrated (P6).

This is where the whole project comes together at query time:

    query
      -> [engine gRPC]  BM25 lexical candidates        (bm25_depth)
      -> [bi-encoder]   dense semantic candidates       (dense_depth)
      -> [RRF]          fuse the two ranked lists        -> fused
      -> [cross-encoder] re-rank the fused top-k         (rerank_depth)
      -> results, each carrying its score at every stage (the "why this result?" panel)

Every stage is optional and degrades gracefully: no embeddings or ENABLE_DENSE=0
falls back to lexical-only; rerank model missing or ENABLE_RERANK=0 skips the precision
stage. So the gateway runs even on a box without torch — it just serves BM25.

The funnel mirrors the offline pipeline in dense/ exactly (same BGE model, same RRF
k=60), so the live demo and the BENCHMARKS numbers come from the same algorithm.
"""
from __future__ import annotations

import threading
from dataclasses import dataclass, field

import numpy as np

from config import Config
from engine_client import EngineClient


@dataclass
class ScoreBreakdown:
    """Everything the UI needs to explain a result's placement."""
    bm25_score: float | None = None
    bm25_rank: int | None = None
    dense_score: float | None = None
    dense_rank: int | None = None
    rrf_score: float | None = None
    rrf_rank: int | None = None
    ce_score: float | None = None      # cross-encoder, only if reranked
    stage: str = "lexical"             # which stage decided the final order


@dataclass
class Result:
    doc_id: str
    rank: int
    text: str
    scores: ScoreBreakdown = field(default_factory=ScoreBreakdown)


def _load_doc_embeddings(npy_path: str, ids_path: str):
    """Load precomputed L2-normalized doc vectors + their ids (from dense/embed.py)."""
    emb = np.load(npy_path).astype(np.float32)
    with open(ids_path, encoding="utf-8") as fh:
        ids = [ln.rstrip("\n") for ln in fh]
    if len(ids) != emb.shape[0]:
        raise ValueError(f"embedding/id count mismatch: {emb.shape[0]} vs {len(ids)}")
    return emb, ids


def _load_corpus_text(corpus_path: str) -> dict[str, str]:
    texts: dict[str, str] = {}
    with open(corpus_path, encoding="utf-8") as fh:
        for line in fh:
            tab = line.find("\t")
            if tab > 0:
                texts[line[:tab]] = line[tab + 1:].rstrip("\n")
    return texts


class Retriever:
    """Holds the funnel's heavy state (embeddings, models) and runs queries."""

    def __init__(self, cfg: Config):
        self.cfg = cfg
        self.engine = EngineClient(cfg.engine_addr)
        self.texts = _load_corpus_text(cfg.corpus_path)

        # Dense state, loaded only if enabled and the embeddings exist on disk.
        self.doc_emb = None
        self.doc_ids: list[str] = []
        self.id_to_row: dict[str, int] = {}
        self._embed_model = None
        self._embed_lock = threading.Lock()
        self.dense_ready = False
        if cfg.enable_dense:
            try:
                self.doc_emb, self.doc_ids = _load_doc_embeddings(cfg.doc_emb_npy, cfg.doc_emb_ids)
                self.id_to_row = {d: i for i, d in enumerate(self.doc_ids)}
                self.dense_ready = True
            except Exception as e:  # noqa: BLE001 - degrade to lexical-only
                print(f"[retrieval] dense disabled (could not load embeddings): {e}")

        # Cross-encoder is loaded lazily on first rerank (heavy import + download).
        self._reranker = None
        self._reranker_lock = threading.Lock()
        self.rerank_enabled = cfg.enable_rerank

    # --- lazy model loaders ---------------------------------------------------
    def _embedder(self):
        if self._embed_model is None:
            with self._embed_lock:
                if self._embed_model is None:
                    from sentence_transformers import SentenceTransformer
                    self._embed_model = SentenceTransformer(self.cfg.embed_model)
        return self._embed_model

    def _rerank_model(self):
        if self._reranker is None:
            with self._reranker_lock:
                if self._reranker is None:
                    from sentence_transformers import CrossEncoder
                    self._reranker = CrossEncoder(self.cfg.rerank_model, max_length=512)
        return self._reranker

    # --- stages ---------------------------------------------------------------
    def _dense_search(self, query: str, depth: int) -> list[tuple[str, float]]:
        """Embed the query and return its top-`depth` (doc_id, cosine) by inner product."""
        q = self._embedder().encode(
            [self.cfg.bge_query_instruction + query],
            normalize_embeddings=True,
        ).astype(np.float32)[0]
        sims = self.doc_emb @ q  # docs are normalized => inner product == cosine
        depth = min(depth, sims.shape[0])
        idx = np.argpartition(-sims, depth - 1)[:depth]
        idx = idx[np.argsort(-sims[idx])]
        return [(self.doc_ids[i], float(sims[i])) for i in idx]

    @staticmethod
    def _rrf(rank_lists: list[list[str]], k: int) -> dict[str, float]:
        """Reciprocal Rank Fusion: each list contributes 1/(k + rank) per doc."""
        scores: dict[str, float] = {}
        for lst in rank_lists:
            for rank, doc_id in enumerate(lst, start=1):
                scores[doc_id] = scores.get(doc_id, 0.0) + 1.0 / (k + rank)
        return scores

    # --- the funnel -----------------------------------------------------------
    def search(self, query: str, top_k: int | None = None) -> dict:
        cfg = self.cfg
        top_k = top_k or cfg.default_top_k

        # 1) Lexical candidates from the C++ engine (with text for rerank/snippets).
        bm25_hits = self.engine.search(query, top_k=cfg.bm25_depth, include_text=True)
        bm25_rank = {h.doc_id: h.rank for h in bm25_hits}
        bm25_score = {h.doc_id: h.bm25_score for h in bm25_hits}
        for h in bm25_hits:  # opportunistically cache any text we didn't already have
            if h.text and h.doc_id not in self.texts:
                self.texts[h.doc_id] = h.text

        # 2) Dense candidates (optional).
        dense_rank: dict[str, int] = {}
        dense_score: dict[str, float] = {}
        if self.dense_ready:
            for rank, (doc_id, sim) in enumerate(self._dense_search(query, cfg.dense_depth), start=1):
                dense_rank[doc_id] = rank
                dense_score[doc_id] = sim

        # 3) Fuse. With no dense stage this is just the BM25 order (RRF of one list).
        rank_lists = [[h.doc_id for h in bm25_hits]]
        used_dense = bool(dense_rank)
        if used_dense:
            rank_lists.append([d for d, _ in sorted(dense_rank.items(), key=lambda kv: kv[1])])
        rrf = self._rrf(rank_lists, cfg.rrf_k)
        fused = sorted(rrf.items(), key=lambda kv: kv[1], reverse=True)
        rrf_rank = {doc_id: i for i, (doc_id, _) in enumerate(fused, start=1)}

        # 4) Re-rank the fused top-k with the cross-encoder (optional).
        applied_rerank = False
        ce_score: dict[str, float] = {}
        ordered = [doc_id for doc_id, _ in fused]
        if self.rerank_enabled and len(ordered) > 0:
            head = ordered[: cfg.rerank_depth]
            pairs = [(query, self.texts.get(d, "")) for d in head]
            try:
                scores = self._rerank_model().predict(pairs)
                ce_score = {d: float(s) for d, s in zip(head, scores)}
                head_sorted = sorted(head, key=lambda d: ce_score[d], reverse=True)
                ordered = head_sorted + ordered[cfg.rerank_depth:]
                applied_rerank = True
            except Exception as e:  # noqa: BLE001 - keep fused order on failure
                print(f"[retrieval] rerank skipped: {e}")

        # 5) Assemble the top_k results with full score provenance.
        results: list[Result] = []
        for final_rank, doc_id in enumerate(ordered[:top_k], start=1):
            sb = ScoreBreakdown(
                bm25_score=bm25_score.get(doc_id),
                bm25_rank=bm25_rank.get(doc_id),
                dense_score=dense_score.get(doc_id),
                dense_rank=dense_rank.get(doc_id),
                rrf_score=rrf.get(doc_id),
                rrf_rank=rrf_rank.get(doc_id),
                ce_score=ce_score.get(doc_id),
                stage=("reranked" if (applied_rerank and doc_id in ce_score)
                       else ("hybrid" if used_dense else "lexical")),
            )
            results.append(Result(doc_id=doc_id, rank=final_rank,
                                  text=self.texts.get(doc_id, ""), scores=sb))

        return {
            "query": query,
            "pipeline": {
                "lexical": True,
                "dense": used_dense,
                "fusion": "rrf" if used_dense else "none",
                "rerank": applied_rerank,
                "rerank_model": cfg.rerank_model if applied_rerank else None,
                "embed_model": cfg.embed_model if used_dense else None,
            },
            "results": results,
        }

    def close(self) -> None:
        self.engine.close()
