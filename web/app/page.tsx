"use client";

import { useState } from "react";
import SearchBox from "@/components/SearchBox";
import WhyPanel, { Ranges } from "@/components/WhyPanel";
import { search, SearchResponse, SearchResult } from "@/lib/api";

// Compute the min/max of each score across the result set, so the WhyPanel bars are
// normalized within the current query (raw scales differ wildly between stages).
function computeRanges(results: SearchResult[]): Ranges {
  const pick = (f: (r: SearchResult) => number | null) => {
    const xs = results.map(f).filter((v): v is number => v !== null && isFinite(v));
    return xs.length ? { min: Math.min(...xs), max: Math.max(...xs) } : { min: 0, max: 1 };
  };
  return {
    bm25: pick((r) => r.scores.bm25_score),
    dense: pick((r) => r.scores.dense_score),
    rrf: pick((r) => r.scores.rrf_score),
    ce: pick((r) => r.scores.ce_score),
  };
}

function ResultCard({ r, ranges }: { r: SearchResult; ranges: Ranges }) {
  const [open, setOpen] = useState(false);
  return (
    <div className="result">
      <div className="head">
        <span className="rank">#{r.rank}</span>
        <span className="docid">{r.doc_id}</span>
        <span className={`badge ${r.scores.stage}`}>{r.scores.stage}</span>
      </div>
      <div className="text">{r.text || <em style={{ opacity: 0.5 }}>(no text)</em>}</div>
      <button className="why-toggle" onClick={() => setOpen((o) => !o)}>
        {open ? "▾ hide score breakdown" : "▸ why this result?"}
      </button>
      {open && <WhyPanel scores={r.scores} ranges={ranges} />}
    </div>
  );
}

export default function Home() {
  const [data, setData] = useState<SearchResponse | null>(null);
  const [loading, setLoading] = useState(false);
  const [error, setError] = useState<string | null>(null);

  async function runSearch(q: string) {
    setLoading(true);
    setError(null);
    try {
      setData(await search(q, 10, true));
    } catch (e) {
      setError(e instanceof Error ? e.message : String(e));
      setData(null);
    } finally {
      setLoading(false);
    }
  }

  const ranges = data ? computeRanges(data.results) : null;
  const p = data?.pipeline;

  return (
    <main className="wrap">
      <div className="brand">
        <h1>
          <span className="kanji">情報</span> Joho
        </h1>
        <span className="tag">explainable search — BM25 · dense · RRF · cross-encoder</span>
      </div>
      <p className="subtitle">
        A from-scratch search engine. Every result shows its score at each stage of the funnel.
      </p>

      <SearchBox onSearch={runSearch} />

      {data && p && (
        <div className="meta">
          <span className={`pill ${p.lexical ? "on" : "off"}`}>BM25</span>
          <span className={`pill ${p.dense ? "on" : "off"}`}>dense{p.embed_model ? ` · ${p.embed_model.split("/").pop()}` : ""}</span>
          <span className={`pill ${p.fusion !== "none" ? "on" : "off"}`}>RRF</span>
          <span className={`pill ${p.rerank ? "on" : "off"}`}>rerank{p.rerank_model ? ` · ${p.rerank_model.split("/").pop()}` : ""}</span>
          {data.corrected_query && (
            <span
              className="didyoumean"
              onClick={() => runSearch(data.corrected_query!)}
              title="search the corrected query"
            >
              searched for <b>{data.corrected_query}</b> (corrected)
            </span>
          )}
        </div>
      )}

      {loading && <p className="status">Searching…</p>}
      {error && <p className="error">⚠ {error}<br />Is the gateway running at the configured URL?</p>}

      {data && !loading && ranges && (
        <div>
          {data.results.length === 0 && <p className="status">No results.</p>}
          {data.results.map((r) => (
            <ResultCard key={r.doc_id} r={r} ranges={ranges} />
          ))}
        </div>
      )}

      <div className="footer">
        Joho · <a href="https://github.com/priyabratamo" target="_blank" rel="noreferrer">portfolio project</a> ·
        the funnel: lexical → semantic → fusion → re-rank
      </div>
    </main>
  );
}
