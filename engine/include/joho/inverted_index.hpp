#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "joho/index_reader.hpp"
#include "joho/tokenizer.hpp"

namespace joho {

// The in-memory inverted index: term -> list of documents that contain it.
// This is the classic "back-of-the-book index", and it is the
// heart of the whole search engine. Implements IndexReader so BM25 can score it.
//
// Usage:
//   InvertedIndex idx;
//   idx.add_document("d1", "the lion hunts");
//   idx.add_document("d2", "a tiger hunts");
//   idx.finalize();              // <-- must call once, computes avg doc length
class InvertedIndex : public IndexReader {
public:
    // Add one document. `external_id` is your own id (e.g. an MS MARCO passage
    // id). Returns the internal doc id (a dense 0-based integer) we assign.
    uint32_t add_document(const std::string& external_id, const std::string& text);

    // Call once after all documents are added. Computes the average document
    // length, which BM25's length-normalization term needs.
    void finalize();

    // --- IndexReader interface ---
    std::size_t num_docs() const override { return doc_ids_.size(); }
    double avgdl() const override { return avgdl_; }
    uint32_t doc_len(uint32_t doc_id) const override { return doc_len_[doc_id]; }
    const std::string& external_id(uint32_t doc_id) const override { return doc_ids_[doc_id]; }
    std::size_t df(const std::string& term) const override;
    void postings(const std::string& term, std::vector<Posting>& out) const override;

    // Zero-copy posting-list accessor (in-memory only — DiskIndex can't return a
    // reference into compressed bytes). Returns an empty list for unknown terms.
    const std::vector<Posting>& postings(const std::string& term) const;

    // The full term -> postings map. Used to serialize the index to disk
    // (disk_index.cpp).
    const std::unordered_map<std::string, std::vector<Posting>>& all_postings() const {
        return postings_;
    }

private:
    Tokenizer tokenizer_;
    std::vector<std::string> doc_ids_;  // internal id -> external id
    std::vector<uint32_t> doc_len_;     // internal id -> number of tokens
    std::unordered_map<std::string, std::vector<Posting>> postings_;  // term -> postings
    double avgdl_ = 0.0;

    static const std::vector<Posting> kEmptyPostings;  // returned for unknown terms
};

}  // namespace joho
