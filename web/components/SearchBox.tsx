"use client";

import { useEffect, useRef, useState } from "react";
import { autocomplete, Completion } from "@/lib/api";

// Search input with a debounced autocomplete dropdown (engine trie via the gateway).
// Arrow keys move through suggestions; Enter searches the current text (or the
// highlighted suggestion); Escape closes the dropdown.
export default function SearchBox({ onSearch }: { onSearch: (q: string) => void }) {
  const [value, setValue] = useState("");
  const [items, setItems] = useState<Completion[]>([]);
  const [open, setOpen] = useState(false);
  const [active, setActive] = useState(-1);
  const timer = useRef<ReturnType<typeof setTimeout> | null>(null);

  useEffect(() => {
    // autocomplete the LAST token of the query (that's the word being typed)
    const lastTok = value.split(/\s+/).pop() || "";
    if (lastTok.length < 2) { setItems([]); setOpen(false); return; }
    if (timer.current) clearTimeout(timer.current);
    timer.current = setTimeout(async () => {
      try {
        const c = await autocomplete(lastTok.toLowerCase(), 8);
        setItems(c);
        setOpen(c.length > 0);
        setActive(-1);
      } catch {
        setItems([]); setOpen(false);
      }
    }, 120);
    return () => { if (timer.current) clearTimeout(timer.current); };
  }, [value]);

  function applyCompletion(term: string) {
    const toks = value.split(/\s+/);
    toks[toks.length - 1] = term;
    const next = toks.join(" ");
    setValue(next);
    setOpen(false);
    onSearch(next);
  }

  function submit() {
    setOpen(false);
    if (active >= 0 && active < items.length) applyCompletion(items[active].term);
    else if (value.trim()) onSearch(value.trim());
  }

  function onKeyDown(e: React.KeyboardEvent) {
    if (!open) { if (e.key === "Enter") submit(); return; }
    if (e.key === "ArrowDown") { e.preventDefault(); setActive((a) => Math.min(a + 1, items.length - 1)); }
    else if (e.key === "ArrowUp") { e.preventDefault(); setActive((a) => Math.max(a - 1, -1)); }
    else if (e.key === "Enter") { e.preventDefault(); submit(); }
    else if (e.key === "Escape") setOpen(false);
  }

  return (
    <div className="searchbar">
      <input
        autoFocus
        placeholder="Search the corpus…  (try: cardiac myocyte regeneration)"
        value={value}
        onChange={(e) => setValue(e.target.value)}
        onKeyDown={onKeyDown}
        onFocus={() => items.length && setOpen(true)}
        onBlur={() => setTimeout(() => setOpen(false), 150)}
      />
      {open && (
        <ul className="suggestions">
          {items.map((c, i) => (
            <li
              key={c.term}
              className={i === active ? "active" : ""}
              onMouseDown={(e) => { e.preventDefault(); applyCompletion(c.term); }}
            >
              <span>{c.term}</span>
              <span className="freq">{c.weight.toLocaleString()}</span>
            </li>
          ))}
        </ul>
      )}
    </div>
  );
}
