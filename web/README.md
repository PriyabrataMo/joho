# `web/` — Joho search UI (P6)

A small Next.js (App Router, TypeScript) front-end for the search funnel. Its one
distinguishing feature is the **"why this result?"** panel: every result expands to
show its score at each stage — BM25, dense cosine, RRF fusion, cross-encoder — so the
ranking is explainable rather than a black box.

It talks only to the **gateway** (REST/JSON); the gateway does the gRPC + ML work.

## Run locally
```bash
cp .env.local.example .env.local      # point NEXT_PUBLIC_GATEWAY_URL at the gateway
npm install
npm run dev                            # http://localhost:3000
```
Requires the gateway (and the engine behind it) to be running — see `../gateway/README.md`.

## Deploy (Vercel)
1. Import the repo into Vercel, set the project root to `web/`.
2. Set the env var **`NEXT_PUBLIC_GATEWAY_URL`** to your deployed gateway URL
   (e.g. the Cloud Run service URL from P8).
3. Deploy. The UI is static + client-side fetch, so it scales to zero cost on Vercel.

## Files
- `app/page.tsx` — the search page (state, results, score-range normalization).
- `components/SearchBox.tsx` — input + debounced autocomplete dropdown (engine trie).
- `components/WhyPanel.tsx` — the per-result score breakdown with normalized bars.
- `lib/api.ts` — typed gateway client (`/search`, `/autocomplete`).
