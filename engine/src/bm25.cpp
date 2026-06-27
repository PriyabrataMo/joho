#include "joho/bm25.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <utility>
#include <vector>

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
    Scratch scratch;  // one-shot buffers for the convenience path
    return search(query, top_k, scratch);
}

std::vector<ScoredDoc> BM25::search(const std::string& query, std::size_t top_k,
                                    Scratch& scratch) const {
    const std::vector<std::string> terms = tokenizer_.tokenize(query);
    const double avgdl = index_.avgdl();

    // Dense accumulator: scores[doc_id] lives at acc[doc_id] directly, so adding a
    // term's contribution is an array write — no hashing, no rehash/alloc as the
    // posting lists (millions of entries for common terms) stream through. acc is
    // sized once to num_docs and held all-zero between queries by the reset below.
    std::vector<double>& acc = scratch.acc;
    std::vector<uint32_t>& touched = scratch.touched;
    std::vector<Posting>& postings = scratch.postings;
    if (acc.size() < index_.num_docs()) acc.assign(index_.num_docs(), 0.0);
    touched.clear();

    // Term-at-a-time scoring: walk each query term's posting list and add its BM25
    // contribution to every document that contains it. The first time a doc is
    // scored (acc still 0) we remember it in `touched` so we can reset cheaply and
    // so it lands in the candidate set exactly once. (Every contribution is > 0, so
    // acc==0 is a reliable "not yet touched" test.)
    for (const std::string& term : terms) {
        const double term_idf = idf(term);
        index_.postings(term, postings);
        for (const Posting& p : postings) {
            const double tf = static_cast<double>(p.tf);
            const double dl = static_cast<double>(index_.doc_len(p.doc_id));

            // BM25 term contribution:
            //   idf * ( tf * (k1 + 1) ) / ( tf + k1 * (1 - b + b * dl/avgdl) )
            const double denom = tf + k1_ * (1.0 - b_ + b_ * dl / avgdl);
            double& slot = acc[p.doc_id];
            if (slot == 0.0) touched.push_back(p.doc_id);
            slot += term_idf * (tf * (k1_ + 1.0)) / denom;
        }
    }

    // Gather the touched docs and take the top_k by score (descending). partial_sort
    // avoids fully sorting when we only need the head of the list.
    std::vector<std::pair<uint32_t, double>> ranked;
    ranked.reserve(touched.size());
    for (const uint32_t doc_id : touched) ranked.emplace_back(doc_id, acc[doc_id]);

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

    // Restore the invariant: acc is all-zero again, ready for the next query. We
    // only touch the entries we actually wrote, so this is O(hits), not O(num_docs).
    for (const uint32_t doc_id : touched) acc[doc_id] = 0.0;
    return results;
}

}  // namespace joho
