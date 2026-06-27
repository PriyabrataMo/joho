#pragma once
#include <cstddef>
#include <string>
#include <vector>

#include "joho/index_reader.hpp"
#include "joho/tokenizer.hpp"

namespace joho {

// A single search result: the document's external id and its BM25 score.
struct ScoredDoc {
    std::string external_id;
    double score;
};

// BM25 ranking over an InvertedIndex.
//
// BM25 scores a (query, document) pair using three intuitions:
//   1. term frequency (tf)  - a doc that uses a query word more is more relevant,
//      but with diminishing returns (controlled by k1).
//   2. inverse document frequency (idf) - rare words matter more than common ones.
//   3. length normalization - long docs shouldn't win just by being long
//      (controlled by b).
//
// Default parameters k1=0.9, b=0.4 match the Anserini MS MARCO baseline that
// scores MRR@10 ~ 0.187 on MS MARCO. The other common defaults are
// k1=1.2, b=0.75 ("classic" BM25). We keep them configurable so we can tune.
class BM25 {
public:
    explicit BM25(const IndexReader& index, double k1 = 0.9, double b = 0.4)
        : index_(index), k1_(k1), b_(b) {}

    // Reusable per-call scratch space. The hot path scores into a DENSE array
    // indexed directly by doc_id (doc_ids are 0..N-1) instead of hashing every
    // posting into an unordered_map — no hashing, no per-posting allocation.
    //
    // `acc` is kept all-zero between queries: we record every doc we touch in
    // `touched` and zero only those entries afterwards, so reset is O(hits), not
    // O(num_docs). One Scratch is NOT thread-safe; give each thread its own (the
    // batch driver does exactly that). Reusing one across queries on a thread
    // avoids re-allocating the (num_docs-sized) accumulator every query.
    struct Scratch {
        std::vector<double> acc;        // doc_id -> running score; 0.0 == untouched
        std::vector<uint32_t> touched;  // doc_ids scored this query (for cheap reset)
        std::vector<Posting> postings;  // posting decode buffer, reused across terms
    };

    // Returns up to `top_k` documents for `query`, highest score first.
    // Convenience overload: allocates a one-shot Scratch internally.
    std::vector<ScoredDoc> search(const std::string& query, std::size_t top_k = 10) const;

    // Same, but scoring through a caller-owned Scratch so the buffers can be
    // reused across many queries (the throughput path used by joho_batch).
    std::vector<ScoredDoc> search(const std::string& query, std::size_t top_k,
                                  Scratch& scratch) const;

private:
    double idf(const std::string& term) const;

    const IndexReader& index_;
    Tokenizer tokenizer_;
    double k1_;
    double b_;
};

}  // namespace joho
