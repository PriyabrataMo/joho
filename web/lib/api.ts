// Typed client for the Joho gateway REST API. The gateway base URL comes from the
// NEXT_PUBLIC_GATEWAY_URL env var (set in .env.local for dev, in Vercel for prod);
// it falls back to localhost:8000 for a local run.

export const GATEWAY_URL =
  process.env.NEXT_PUBLIC_GATEWAY_URL?.replace(/\/$/, "") || "http://localhost:8000";

export interface ScoreBreakdown {
  bm25_score: number | null;
  bm25_rank: number | null;
  dense_score: number | null;
  dense_rank: number | null;
  rrf_score: number | null;
  rrf_rank: number | null;
  ce_score: number | null;
  stage: string;
}

export interface SearchResult {
  doc_id: string;
  rank: number;
  text: string;
  scores: ScoreBreakdown;
}

export interface Pipeline {
  lexical: boolean;
  dense: boolean;
  fusion: string;
  rerank: boolean;
  rerank_model: string | null;
  embed_model: string | null;
}

export interface SearchResponse {
  query: string;
  corrected_query: string | null;
  pipeline: Pipeline;
  results: SearchResult[];
}

export interface Completion {
  term: string;
  weight: number;
}

async function getJSON<T>(path: string): Promise<T> {
  const res = await fetch(`${GATEWAY_URL}${path}`);
  if (!res.ok) throw new Error(`gateway ${res.status}: ${await res.text()}`);
  return res.json() as Promise<T>;
}

export function search(q: string, k = 10, autocorrect = true): Promise<SearchResponse> {
  const params = new URLSearchParams({ q, k: String(k), autocorrect: String(autocorrect) });
  return getJSON<SearchResponse>(`/search?${params}`);
}

export async function autocomplete(prefix: string, k = 8): Promise<Completion[]> {
  const params = new URLSearchParams({ q: prefix, k: String(k) });
  const data = await getJSON<{ completions: Completion[] }>(`/autocomplete?${params}`);
  return data.completions;
}
