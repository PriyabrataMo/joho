"""Thin gRPC client to the C++ engine (joho_server) — the lexical + query-assist layer.

The gateway never re-implements BM25 or the trie/SymSpell; it calls the engine over
the contract in proto/joho.proto. This module wraps the generated stub in a small,
typed facade so the rest of the gateway deals in plain dicts/dataclasses, not protobuf.
"""
from __future__ import annotations

from dataclasses import dataclass

import grpc

# Generated from proto/joho.proto by scripts/gen_proto.sh into gateway/joho_pb/.
from joho_pb import joho_pb2, joho_pb2_grpc


@dataclass
class BM25Hit:
    doc_id: str
    bm25_score: float
    rank: int
    text: str = ""


class EngineClient:
    def __init__(self, addr: str):
        # 64 MB receive limit: top-1000 hits with text can exceed gRPC's 4 MB default.
        opts = [("grpc.max_receive_message_length", 64 * 1024 * 1024)]
        self._channel = grpc.insecure_channel(addr, options=opts)
        self._stub = joho_pb2_grpc.JohoStub(self._channel)

    def search(self, query: str, top_k: int, include_text: bool = True) -> list[BM25Hit]:
        resp = self._stub.Search(
            joho_pb2.SearchRequest(query=query, top_k=top_k, include_text=include_text)
        )
        return [BM25Hit(h.doc_id, h.bm25_score, h.rank, h.text) for h in resp.hits]

    def autocomplete(self, prefix: str, k: int = 8) -> list[dict]:
        resp = self._stub.Autocomplete(joho_pb2.AutocompleteRequest(prefix=prefix, k=k))
        return [{"term": c.term, "weight": c.weight} for c in resp.completions]

    def correct(self, word: str, k: int = 5) -> dict:
        resp = self._stub.Correct(joho_pb2.CorrectRequest(word=word, k=k))
        return {
            "found": resp.found,
            "term": resp.term,
            "distance": resp.distance,
            "frequency": resp.frequency,
            "alternatives": list(resp.alternatives),
        }

    def health(self) -> dict:
        resp = self._stub.Health(joho_pb2.HealthRequest())
        return {
            "ok": resp.ok,
            "num_docs": resp.num_docs,
            "index_name": resp.index_name,
            "suggest_ready": resp.suggest_ready,
        }

    def close(self) -> None:
        self._channel.close()
