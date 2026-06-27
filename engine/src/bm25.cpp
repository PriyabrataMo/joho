#include "joho/bm25.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <utility>
#include <vector>

#include "joho/inverted_index.hpp"

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

// ---------------------------------------------------------------------------
// WAND — document-at-a-time retrieval with dynamic top-k pruning.
//
// The exhaustive search() above is term-at-a-time: it scores EVERY document that
// contains ANY query term. WAND instead walks all the query terms' posting lists
// together in doc-id order, and uses a per-term upper bound on each term's BM25
// contribution to skip whole runs of documents that cannot possibly beat the
// current k-th best score (the "threshold", theta).
//
// The mechanics, each step:
//   1. Sort the term cursors by their current doc id.
//   2. Walk them in that order summing the per-term upper bounds; the first
//      cursor at which the running sum reaches theta is the "pivot". No document
//      smaller than the pivot's doc id can reach theta, so they're all skipped.
//   3. If the smallest cursor is already AT the pivot doc, score it fully and try
//      to insert it into the top-k heap (which may raise theta). Otherwise binary-
//      search the lagging cursor forward to the pivot doc — the actual "leap".
//
// Correctness: the bound impact(max_tf, min_len) is >= every real contribution,
// and the pivot test uses >= theta (not >), so a document that could merely TIE
// the threshold is still scored (it may win the doc-id tiebreak). Hence WAND
// returns exactly the same top-k as the exhaustive scan (identical IR metrics).
// ---------------------------------------------------------------------------
std::vector<ScoredDoc> BM25::search_wand(const std::string& query, std::size_t top_k,
                                         Scratch& scratch) const {
    // WAND needs zero-copy posting lists and the precomputed per-term bounds, both
    // of which only the in-memory InvertedIndex offers. For any other backend
    // (e.g. the mmapped DiskIndex) fall back to the exhaustive path — same answer.
    const InvertedIndex* mem = dynamic_cast<const InvertedIndex*>(&index_);
    if (mem == nullptr) return search(query, top_k, scratch);

    const std::vector<std::string> terms = tokenizer_.tokenize(query);
    if (terms.empty() || top_k == 0) return {};

    const double avgdl = index_.avgdl();
    const double k1 = k1_;
    const double b = b_;

    // One cursor per query-term OCCURRENCE. Repeats are kept as separate cursors
    // (rather than folding into a multiplicity) so a doc's score is summed from the
    // exact same set of additions, in the same order, as the term-at-a-time path.
    struct Cursor {
        const Posting* cur;  // current posting (advances forward only)
        const Posting* end;
        double idf;
        double ub;  // upper bound on this term's contribution to any single doc
    };
    std::vector<Cursor> cursors;
    cursors.reserve(terms.size());
    for (const std::string& term : terms) {
        const std::vector<Posting>& pl = mem->postings(term);
        if (pl.empty()) continue;  // term not in the index -> contributes nothing
        uint32_t max_tf = 0, min_len = 0;
        mem->term_bound(term, max_tf, min_len);  // always present for a non-empty list
        const double term_idf = idf(term);
        // impact(tf, dl) = tf*(k1+1) / (tf + k1*(1 - b + b*dl/avgdl)); rises with
        // tf, falls with dl, so (max_tf, min_len) maximizes it -> a safe bound.
        const double denom = max_tf + k1 * (1.0 - b + b * static_cast<double>(min_len) / avgdl);
        const double ub = term_idf * (static_cast<double>(max_tf) * (k1 + 1.0)) / denom;
        cursors.push_back(Cursor{pl.data(), pl.data() + pl.size(), term_idf, ub});
    }
    if (cursors.empty()) return {};

    // Top-k as a binary heap of (score, doc) under the total order `better`
    // (higher score, then smaller doc id). The heap comparator IS `better`, so
    // heap.front() is the WORST kept doc — the eviction candidate, and the one
    // whose score is the pruning threshold theta once the heap is full.
    struct Entry {
        double score;
        uint32_t doc;
    };
    auto better = [](const Entry& a, const Entry& b) {
        return a.score > b.score || (a.score == b.score && a.doc < b.doc);
    };
    std::vector<Entry> heap;
    heap.reserve(top_k);

    const double NEG_INF = -std::numeric_limits<double>::infinity();
    const uint32_t SENT = std::numeric_limits<uint32_t>::max();
    auto cur_doc = [&](const Cursor& c) -> uint32_t {
        return c.cur != c.end ? c.cur->doc_id : SENT;
    };
    auto contribution = [&](const Cursor& c, uint32_t tf, uint32_t doc) -> double {
        const double dl = static_cast<double>(index_.doc_len(doc));
        const double denom = tf + k1 * (1.0 - b + b * dl / avgdl);
        return c.idf * (static_cast<double>(tf) * (k1 + 1.0)) / denom;
    };

    std::vector<uint32_t> order;  // cursor indices, sorted by current doc each step
    order.reserve(cursors.size());

    for (;;) {
        order.clear();
        for (uint32_t i = 0; i < cursors.size(); ++i)
            if (cursors[i].cur != cursors[i].end) order.push_back(i);
        if (order.empty()) break;  // every posting list exhausted
        std::sort(order.begin(), order.end(), [&](uint32_t a, uint32_t b) {
            return cur_doc(cursors[a]) < cur_doc(cursors[b]);
        });

        const double theta = (heap.size() == top_k) ? heap.front().score : NEG_INF;

        // Pivot: first cursor whose cumulative UB reaches theta. >= (not >) so a
        // doc that can only tie theta is still considered.
        double acc_ub = 0.0;
        int pivot = -1;
        for (uint32_t j = 0; j < order.size(); ++j) {
            acc_ub += cursors[order[j]].ub;
            if (acc_ub >= theta) { pivot = static_cast<int>(j); break; }
        }
        if (pivot < 0) break;  // no surviving document can reach theta -> done

        const uint32_t pivot_doc = cur_doc(cursors[order[pivot]]);

        if (cur_doc(cursors[order[0]]) == pivot_doc) {
            // The pivot doc is the current frontier: score it from every cursor
            // sitting on it. Iterate cursors in their ORIGINAL (tokenized) order so
            // the floating-point sum matches the exhaustive path's accumulation.
            double score = 0.0;
            for (Cursor& c : cursors)
                if (c.cur != c.end && c.cur->doc_id == pivot_doc)
                    score += contribution(c, c.cur->tf, pivot_doc);
            for (Cursor& c : cursors)
                if (c.cur != c.end && c.cur->doc_id == pivot_doc) ++c.cur;

            const Entry e{score, pivot_doc};
            if (heap.size() < top_k) {
                heap.push_back(e);
                std::push_heap(heap.begin(), heap.end(), better);
            } else if (better(e, heap.front())) {
                std::pop_heap(heap.begin(), heap.end(), better);
                heap.back() = e;
                std::push_heap(heap.begin(), heap.end(), better);
            }
        } else {
            // The smallest cursor lags behind the pivot doc: leap it forward with a
            // binary search instead of stepping one posting at a time. This skip is
            // where WAND saves work over the exhaustive scan.
            Cursor& c = cursors[order[0]];
            c.cur = std::lower_bound(
                c.cur, c.end, pivot_doc,
                [](const Posting& p, uint32_t d) { return p.doc_id < d; });
        }
    }

    std::sort(heap.begin(), heap.end(), better);
    std::vector<ScoredDoc> results;
    results.reserve(heap.size());
    for (const Entry& e : heap)
        results.push_back(ScoredDoc{index_.external_id(e.doc), e.score});
    return results;
}

}  // namespace joho
