"use client";

import { ScoreBreakdown } from "@/lib/api";

export interface Range { min: number; max: number }
export interface Ranges { bm25: Range; dense: Range; rrf: Range; ce: Range }

// Map a raw score to a 0..1 bar fraction within the query's observed range, so bars
// are comparable across results even when raw scales differ (BM25 ~tens, cosine 0..1,
// RRF ~0.03, cross-encoder logits possibly negative). Min-max keeps it honest.
function frac(value: number | null, r: Range): number {
  if (value === null || !isFinite(value)) return 0;
  if (r.max <= r.min) return 1;
  return Math.max(0.04, Math.min(1, (value - r.min) / (r.max - r.min)));
}

function Row(props: {
  label: string;
  cls: string;
  value: number | null;
  rank: number | null;
  range: Range;
  digits?: number;
}) {
  const { label, cls, value, rank, range, digits = 3 } = props;
  const has = value !== null && value !== undefined;
  return (
    <>
      <div className="label">
        {label}
        {has && rank ? <span style={{ opacity: 0.6 }}> · #{rank}</span> : null}
      </div>
      <div className={`bar ${cls}`}>
        {has ? <span style={{ width: `${frac(value, range) * 100}%` }} /> : null}
      </div>
      <div className="val">{has ? value!.toFixed(digits) : <span className="none">—</span>}</div>
    </>
  );
}

export default function WhyPanel({ scores, ranges }: { scores: ScoreBreakdown; ranges: Ranges }) {
  return (
    <div className="why">
      <Row label="BM25" cls="bm25" value={scores.bm25_score} rank={scores.bm25_rank} range={ranges.bm25} digits={2} />
      <Row label="Dense (cos)" cls="dense" value={scores.dense_score} rank={scores.dense_rank} range={ranges.dense} />
      <Row label="RRF fusion" cls="rrf" value={scores.rrf_score} rank={scores.rrf_rank} range={ranges.rrf} digits={4} />
      <Row label="Cross-enc" cls="ce" value={scores.ce_score} rank={null} range={ranges.ce} digits={3} />
    </div>
  );
}
