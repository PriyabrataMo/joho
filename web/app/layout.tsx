import type { Metadata } from "next";
import "./globals.css";

export const metadata: Metadata = {
  title: "Joho — explainable search",
  description: "A from-scratch search engine: BM25 + dense + RRF + cross-encoder, with a 'why this result?' score panel.",
};

export default function RootLayout({ children }: { children: React.ReactNode }) {
  return (
    <html lang="en">
      <body>{children}</body>
    </html>
  );
}
