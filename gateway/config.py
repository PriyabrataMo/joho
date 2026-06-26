"""Gateway configuration (P6), all overridable via environment variables.

The gateway is the one place that knows the whole funnel's wiring, so every knob
lives here: where the engine is, where the data/embeddings are, which model names
to use, and which stages are switched on. Defaults point at the SciFact dataset
already exported under data/, so a fresh checkout runs with zero flags.
"""
from __future__ import annotations

import os
from dataclasses import dataclass, field


def _bool(name: str, default: bool) -> bool:
    return os.environ.get(name, str(default)).strip().lower() in ("1", "true", "yes", "on")


def _here(*parts: str) -> str:
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))  # repo root
    return os.path.join(root, *parts)


@dataclass
class Config:
    # --- engine (C++ gRPC server) ---
    engine_addr: str = os.environ.get("JOHO_ENGINE_ADDR", "localhost:50051")

    # --- data: corpus text + precomputed document embeddings (from dense/embed.py) ---
    dataset_dir: str = os.environ.get("JOHO_DATASET_DIR", _here("data", "beir_scifact_test"))
    corpus_file: str = os.environ.get("JOHO_CORPUS_FILE", "corpus.tsv")
    doc_emb_prefix: str = os.environ.get("JOHO_DOC_EMB_PREFIX", "corpus_bge")  # .npy + .ids.txt

    # --- funnel stages ---
    enable_dense: bool = _bool("JOHO_ENABLE_DENSE", True)
    enable_rerank: bool = _bool("JOHO_ENABLE_RERANK", True)
    enable_spellcheck: bool = _bool("JOHO_ENABLE_SPELLCHECK", True)

    # --- depths (the funnel narrowing) ---
    bm25_depth: int = int(os.environ.get("JOHO_BM25_DEPTH", "200"))      # lexical candidates
    dense_depth: int = int(os.environ.get("JOHO_DENSE_DEPTH", "200"))    # dense candidates
    rrf_k: int = int(os.environ.get("JOHO_RRF_K", "60"))                 # RRF constant
    rerank_depth: int = int(os.environ.get("JOHO_RERANK_DEPTH", "50"))   # cross-encoder budget
    default_top_k: int = int(os.environ.get("JOHO_TOP_K", "10"))         # results to the UI

    # --- models (match the dense/ pipeline so scores are comparable) ---
    embed_model: str = os.environ.get("JOHO_EMBED_MODEL", "BAAI/bge-small-en-v1.5")
    rerank_model: str = os.environ.get("JOHO_RERANK_MODEL", "BAAI/bge-reranker-base")
    bge_query_instruction: str = field(
        default_factory=lambda: os.environ.get(
            "JOHO_BGE_QUERY_INSTRUCTION",
            "Represent this sentence for searching relevant passages: ",
        )
    )

    # CORS origin for the Next.js UI (comma-separated; "*" to allow all in dev).
    cors_origins: str = os.environ.get("JOHO_CORS_ORIGINS", "*")

    @property
    def corpus_path(self) -> str:
        return os.path.join(self.dataset_dir, self.corpus_file)

    @property
    def doc_emb_npy(self) -> str:
        return os.path.join(self.dataset_dir, self.doc_emb_prefix + ".npy")

    @property
    def doc_emb_ids(self) -> str:
        return os.path.join(self.dataset_dir, self.doc_emb_prefix + ".ids.txt")


config = Config()
