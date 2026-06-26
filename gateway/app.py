"""Joho gateway — FastAPI REST front for the retrieval funnel (P6).

The browser speaks plain JSON to this gateway; the gateway speaks gRPC to the C++
engine and runs the dense/fusion/rerank stages in-process. Endpoints:

    GET  /healthz                 — gateway + engine health
    GET  /autocomplete?q=...      — prefix completions (engine trie)
    GET  /correct?q=...           — "did you mean?" (engine SymSpell)
    GET  /search?q=...&k=...      — the full funnel, with per-result score breakdown

Run:  uvicorn app:app --host 0.0.0.0 --port 8000
"""
from __future__ import annotations

from contextlib import asynccontextmanager

from fastapi import FastAPI, Query
from fastapi.middleware.cors import CORSMiddleware
from pydantic import BaseModel

from config import config
from retrieval import Retriever

# Built once at startup (loads embeddings + connects to the engine), reused per request.
state: dict[str, Retriever] = {}


@asynccontextmanager
async def lifespan(_: FastAPI):
    state["retriever"] = Retriever(config)
    print(f"[gateway] ready: engine={config.engine_addr} dataset={config.dataset_dir} "
          f"dense={state['retriever'].dense_ready} rerank={config.enable_rerank}")
    yield
    state["retriever"].close()


app = FastAPI(title="Joho Search Gateway", version="1.0", lifespan=lifespan)
app.add_middleware(
    CORSMiddleware,
    allow_origins=[o.strip() for o in config.cors_origins.split(",")] if config.cors_origins != "*" else ["*"],
    allow_methods=["*"],
    allow_headers=["*"],
)


# --- response models (so the OpenAPI docs are typed) ---
class ScoreModel(BaseModel):
    bm25_score: float | None = None
    bm25_rank: int | None = None
    dense_score: float | None = None
    dense_rank: int | None = None
    rrf_score: float | None = None
    rrf_rank: int | None = None
    ce_score: float | None = None
    stage: str


class ResultModel(BaseModel):
    doc_id: str
    rank: int
    text: str
    scores: ScoreModel


class SearchResponse(BaseModel):
    query: str
    corrected_query: str | None = None
    pipeline: dict
    results: list[ResultModel]


def _retriever() -> Retriever:
    return state["retriever"]


@app.get("/healthz")
def healthz() -> dict:
    r = _retriever()
    try:
        engine = r.engine.health()
    except Exception as e:  # noqa: BLE001
        engine = {"ok": False, "error": str(e)}
    return {
        "gateway_ok": True,
        "engine": engine,
        "dense_ready": r.dense_ready,
        "rerank_enabled": r.rerank_enabled,
    }


@app.get("/autocomplete")
def autocomplete(q: str = Query(..., min_length=1), k: int = 8) -> dict:
    return {"prefix": q, "completions": _retriever().engine.autocomplete(q, k)}


@app.get("/correct")
def correct(q: str = Query(..., min_length=1)) -> dict:
    return {"word": q, **_retriever().engine.correct(q)}


def _maybe_correct(r: Retriever, query: str) -> str | None:
    """Per-token spell-fix: if a token has a distance-1+ correction, swap it in.
    Returns the corrected query string, or None if nothing changed."""
    if not r.cfg.enable_spellcheck:
        return None
    tokens = query.split()
    fixed, changed = [], False
    for tok in tokens:
        c = r.engine.correct(tok.lower())
        if c["found"] and c["distance"] > 0:
            fixed.append(c["term"])
            changed = True
        else:
            fixed.append(tok)
    return " ".join(fixed) if changed else None


@app.get("/search", response_model=SearchResponse)
def search(
    q: str = Query(..., min_length=1, description="the search query"),
    k: int = Query(None, description="results to return"),
    autocorrect: bool = Query(True, description="apply spell correction before searching"),
) -> SearchResponse:
    r = _retriever()
    corrected = _maybe_correct(r, q) if autocorrect else None
    effective = corrected or q
    out = r.search(effective, top_k=k)
    return SearchResponse(
        query=q,
        corrected_query=corrected,
        pipeline=out["pipeline"],
        results=[
            ResultModel(
                doc_id=res.doc_id, rank=res.rank, text=res.text,
                scores=ScoreModel(**res.scores.__dict__),
            )
            for res in out["results"]
        ],
    )
