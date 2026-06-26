#include "joho/bm25.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <unordered_map>
#include <utility>

namespace joho {

// Inverse document frequency. Rare terms (small df) get a high weight; terms in
// almost every document get a weight near zero.
//
// We use the Lucene/BM25 variant with "1 +" inside the log, which guarantees a
// non-negative idf even for very common terms (the textbook formula without the
// "1 +" can go negative, which causes odd behaviour).
double BM25::idf(const std::string& term) const {
    const double N = static_cast<double>(index_.num_docs());
    const double df = static_cast<double>(index_.df(term));
    return std::log(1.0 + (N - df + 0.5) / (df + 0.5));
}

std::vector<ScoredDoc> BM25::search(const std::string& query, std::size_t top_k) const {
    const std::vector<std::string> terms = tokenizer_.tokenize(query);
    const double avgdl = index_.avgdl();

    // Accumulate a BM25 score per document. We walk one query term at a time and
    // add that term's contribution to every document in its posting list — this
    // is the classic "term-at-a-time" scoring strategy.
    std::unordered_map<uint32_t, double> scores;
    std::vector<Posting> postings;  // reused across query terms (no per-term alloc)
    for (const std::string& term : terms) {
        const double term_idf = idf(term);
        index_.postings(term, postings);
        for (const Posting& p : postings) {
            const double tf = static_cast<double>(p.tf);
            const double dl = static_cast<double>(index_.doc_len(p.doc_id));

            // BM25 term contribution:
            //   idf * ( tf * (k1 + 1) ) / ( tf + k1 * (1 - b + b * dl/avgdl) )
            const double denom = tf + k1_ * (1.0 - b_ + b_ * dl / avgdl);
            scores[p.doc_id] += term_idf * (tf * (k1_ + 1.0)) / denom;
        }
    }

    // Take the top_k documents by score (descending). partial_sort avoids fully
    // sorting when we only need the head of the list.
    std::vector<std::pair<uint32_t, double>> ranked(scores.begin(), scores.end());
    const std::size_t k = std::min(top_k, ranked.size());
    std::partial_sort(
        ranked.begin(), ranked.begin() + static_cast<std::ptrdiff_t>(k), ranked.end(),
        [](const std::pair<uint32_t, double>& a, const std::pair<uint32_t, double>& b) {
            return a.second > b.second;
        });

    std::vector<ScoredDoc> results;
    results.reserve(k);
    for (std::size_t i = 0; i < k; ++i) {
        results.push_back(ScoredDoc{index_.external_id(ranked[i].first), ranked[i].second});
    }
    return results;
}

}  // namespace joho
