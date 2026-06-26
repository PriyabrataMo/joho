#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace joho {

// One entry in a posting list: a document that contains a term, plus how many
// times the term appears in it ("term frequency", tf). Defined here (not in
// inverted_index.hpp) because both the in-memory and on-disk indexes use it.
struct Posting {
    uint32_t doc_id;  // internal id, 0..N-1
    uint32_t tf;      // occurrences of the term within this document
};

// The read interface BM25 needs from an index, regardless of where the data
// lives. Two implementations:
//   * InvertedIndex  — built in RAM from a corpus (inverted_index.hpp)
//   * DiskIndex      — memory-mapped from a compressed file (disk_index.hpp)
//
// BM25 holds a `const IndexReader&`, so the same scorer runs over either backend
// chosen at runtime. The virtual calls are cheap here: `postings()` is invoked
// once per QUERY TERM (a handful per query), and the hot inner loop over the
// returned postings has no virtual dispatch at all.
class IndexReader {
public:
    virtual ~IndexReader() = default;

    // Number of documents in the index.
    virtual std::size_t num_docs() const = 0;

    // Average document length (in tokens) — BM25's length-normalization needs it.
    virtual double avgdl() const = 0;

    // Length (in tokens) of the document with this internal id.
    virtual uint32_t doc_len(uint32_t doc_id) const = 0;

    // The caller-facing id (e.g. an MS MARCO passage id) for an internal id.
    virtual const std::string& external_id(uint32_t doc_id) const = 0;

    // Document frequency: how many documents contain `term`.
    virtual std::size_t df(const std::string& term) const = 0;

    // Decode the posting list for `term` into `out` (cleared first; empty if the
    // term is absent). A fill-buffer API so callers can reuse one vector across
    // query terms — no per-term allocation, and it lets DiskIndex decode straight
    // into the caller's buffer.
    virtual void postings(const std::string& term, std::vector<Posting>& out) const = 0;
};

}  // namespace joho
