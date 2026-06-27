#include "joho/inverted_index.hpp"

namespace joho {

const std::vector<Posting> InvertedIndex::kEmptyPostings;

uint32_t InvertedIndex::add_document(const std::string& external_id,
                                     const std::string& text) {
    const uint32_t doc_id = static_cast<uint32_t>(doc_ids_.size());
    doc_ids_.push_back(external_id);

    const std::vector<std::string> tokens = tokenizer_.tokenize(text);
    doc_len_.push_back(static_cast<uint32_t>(tokens.size()));

    // Count how many times each term occurs in THIS document.
    std::unordered_map<std::string, uint32_t> tf;
    tf.reserve(tokens.size());
    for (const std::string& tok : tokens) ++tf[tok];

    // Append a posting to each term's list. We process documents in increasing
    // id order, so every posting list naturally stays sorted by doc_id — a
    // property we'll rely on later for compression and fast intersection.
    const uint32_t this_len = static_cast<uint32_t>(tokens.size());
    for (const auto& [term, count] : tf) {
        std::vector<Posting>& pl = postings_[term];
        pl.push_back(Posting{doc_id, count});

        // Keep the WAND bound current: the max tf and min doc length over this
        // term's postings so far. On the first posting we seed both values
        // (min_len starts at this doc's length, not 0).
        TermBound& tb = bounds_[term];
        if (pl.size() == 1) {
            tb.max_tf = count;
            tb.min_len = this_len;
        } else {
            if (count > tb.max_tf) tb.max_tf = count;
            if (this_len < tb.min_len) tb.min_len = this_len;
        }
    }
    return doc_id;
}

void InvertedIndex::finalize() {
    uint64_t total_tokens = 0;
    for (uint32_t len : doc_len_) total_tokens += len;
    avgdl_ = doc_ids_.empty()
                 ? 0.0
                 : static_cast<double>(total_tokens) / static_cast<double>(doc_ids_.size());
}

std::size_t InvertedIndex::df(const std::string& term) const {
    auto it = postings_.find(term);
    return it == postings_.end() ? 0 : it->second.size();
}

const std::vector<Posting>& InvertedIndex::postings(const std::string& term) const {
    auto it = postings_.find(term);
    return it == postings_.end() ? kEmptyPostings : it->second;
}

void InvertedIndex::postings(const std::string& term, std::vector<Posting>& out) const {
    auto it = postings_.find(term);
    if (it == postings_.end()) out.clear();
    else out = it->second;  // copy into the caller's reusable buffer
}

bool InvertedIndex::term_bound(const std::string& term, uint32_t& max_tf,
                               uint32_t& min_len) const {
    auto it = bounds_.find(term);
    if (it == bounds_.end()) return false;
    max_tf = it->second.max_tf;
    min_len = it->second.min_len;
    return true;
}

}  // namespace joho
