#pragma once
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "joho/index_reader.hpp"
#include "joho/inverted_index.hpp"

namespace joho {

// Size accounting returned by write_index(), so callers can report a
// raw-vs-compressed table.
struct IndexStats {
    uint64_t num_docs = 0;
    uint64_t num_terms = 0;
    uint64_t num_postings = 0;
    uint64_t raw_postings_bytes = 0;         // num_postings * 8 (uint32 doc_id + uint32 tf)
    uint64_t compressed_postings_bytes = 0;  // after delta + varint encoding
    uint64_t total_file_bytes = 0;           // whole serialized index on disk
};

// Serialize an in-memory InvertedIndex to a compact on-disk index at `path`.
//
// On-disk layout (little-endian host; a portable build would byte-swap):
//
//   [header]   magic "JOHOIX01" | uint32 num_docs | uint32 num_terms | double avgdl
//   [doclens]  uint32 doc_len[num_docs]
//   [docids]   for each doc: varint(id_len) + id_bytes        (internal-id order)
//   [terms]    for each term (sorted): varint(term_len) + term_bytes
//                                       varint(df) + varint(postings_len)
//                                       postings: delta-varint(doc_id gap) + varint(tf), repeated
//
// Posting lists are stored as gaps (delta encoding) so the integers stay small,
// then varint-packed — the two ideas that make the index small.
//
// Throws std::runtime_error if the file cannot be opened.
IndexStats write_index(const InvertedIndex& index, const std::string& path);

// A read-only index backed by an mmap of a file written by write_index().
//
// "Loading" is just an mmap + one scan to build a small in-RAM term dictionary
// (term -> where its postings live in the mapped file). The bulk data — the
// compressed posting lists — stays on disk and is paged in by the OS on demand,
// and decoded only when a query touches it. So startup is near-instant regardless
// of index size, and memory use tracks what queries actually read.
//
// (The in-RAM term dictionary is the part a future FST (P5) would also move
// on-disk; for now it's a hashmap, which is simple and fast.)
class DiskIndex : public IndexReader {
public:
    explicit DiskIndex(const std::string& path);  // mmap + parse dictionary
    ~DiskIndex() override;                          // munmap

    DiskIndex(const DiskIndex&) = delete;
    DiskIndex& operator=(const DiskIndex&) = delete;

    std::size_t num_docs() const override { return num_docs_; }
    double avgdl() const override { return avgdl_; }
    uint32_t doc_len(uint32_t doc_id) const override;
    const std::string& external_id(uint32_t doc_id) const override { return ext_ids_[doc_id]; }
    std::size_t df(const std::string& term) const override;
    void postings(const std::string& term, std::vector<Posting>& out) const override;

private:
    // Where one term's compressed postings live inside the mapped file.
    struct TermEntry {
        const uint8_t* ptr;  // start of the postings blob (into the mmap)
        uint32_t len;        // its length in bytes
        uint32_t df;         // number of postings (for reserve / df())
    };

    int fd_ = -1;
    const uint8_t* base_ = nullptr;  // start of the mmapped file
    std::size_t map_size_ = 0;
    uint32_t num_docs_ = 0;
    double avgdl_ = 0.0;
    const uint8_t* doc_lens_ = nullptr;  // -> uint32 doc_len[num_docs] inside the mmap
    std::vector<std::string> ext_ids_;   // internal id -> external id (decoded at load)
    std::unordered_map<std::string, TermEntry> dict_;  // term -> postings location
};

}  // namespace joho
