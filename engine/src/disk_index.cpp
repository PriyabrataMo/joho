#include "joho/disk_index.hpp"

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <vector>

#include "joho/varint.hpp"

namespace joho {
namespace {

// Raw little-endian writes. arm64/x86 are little-endian, so we write the bytes as
// they sit in memory. (A cross-endian build would serialize byte-by-byte; noted
// in disk_index.hpp.)
void write_raw(std::ofstream& os, const void* p, std::size_t n) {
    os.write(reinterpret_cast<const char*>(p), static_cast<std::streamsize>(n));
}
void write_u32(std::ofstream& os, uint32_t v) { write_raw(os, &v, sizeof(v)); }
void write_f64(std::ofstream& os, double v) { write_raw(os, &v, sizeof(v)); }

}  // namespace

IndexStats write_index(const InvertedIndex& index, const std::string& path) {
    std::ofstream os(path, std::ios::binary);
    if (!os) throw std::runtime_error("cannot open index file for writing: " + path);

    const auto& postings_map = index.all_postings();
    const uint32_t num_docs = static_cast<uint32_t>(index.num_docs());
    const uint32_t num_terms = static_cast<uint32_t>(postings_map.size());

    // --- header ---
    static const char kMagic[8] = {'J', 'O', 'H', 'O', 'I', 'X', '0', '1'};
    write_raw(os, kMagic, 8);
    write_u32(os, num_docs);
    write_u32(os, num_terms);
    write_f64(os, index.avgdl());

    // --- doc lengths (fixed uint32 array, indexable by internal doc id) ---
    for (uint32_t i = 0; i < num_docs; ++i) write_u32(os, index.doc_len(i));

    // --- external ids (varint length-prefixed, in internal-id order) ---
    {
        std::vector<uint8_t> len_buf;
        for (uint32_t i = 0; i < num_docs; ++i) {
            const std::string& id = index.external_id(i);
            len_buf.clear();
            put_varint(len_buf, static_cast<uint32_t>(id.size()));
            write_raw(os, len_buf.data(), len_buf.size());
            write_raw(os, id.data(), id.size());
        }
    }

    // --- dictionary + compressed postings, terms in sorted order ---
    std::vector<const std::string*> terms;
    terms.reserve(postings_map.size());
    for (const auto& kv : postings_map) terms.push_back(&kv.first);
    std::sort(terms.begin(), terms.end(),
              [](const std::string* a, const std::string* b) { return *a < *b; });

    IndexStats stats{};
    stats.num_docs = num_docs;
    stats.num_terms = num_terms;

    std::vector<uint8_t> hdr;   // small per-term header bytes
    std::vector<uint8_t> post;  // encoded postings for the current term
    for (const std::string* term : terms) {
        const std::vector<Posting>& pl = postings_map.at(*term);

        // Delta + varint encode: store the GAP between consecutive doc ids (the
        // list is sorted ascending), then the term frequency.
        post.clear();
        uint32_t prev = 0;
        for (const Posting& p : pl) {
            put_varint(post, p.doc_id - prev);  // gap (first gap == doc_id - 0)
            put_varint(post, p.tf);
            prev = p.doc_id;
        }

        // term_len + term bytes
        hdr.clear();
        put_varint(hdr, static_cast<uint32_t>(term->size()));
        write_raw(os, hdr.data(), hdr.size());
        write_raw(os, term->data(), term->size());

        // df + postings length + postings blob
        hdr.clear();
        put_varint(hdr, static_cast<uint32_t>(pl.size()));    // df
        put_varint(hdr, static_cast<uint32_t>(post.size()));  // postings byte length
        write_raw(os, hdr.data(), hdr.size());
        write_raw(os, post.data(), post.size());

        stats.num_postings += pl.size();
        stats.compressed_postings_bytes += post.size();
    }

    stats.raw_postings_bytes = stats.num_postings * 8;  // 4-byte doc_id + 4-byte tf
    os.flush();
    stats.total_file_bytes = static_cast<uint64_t>(os.tellp());
    return stats;
}

// ---------------------------------------------------------------------------
// DiskIndex: mmap a write_index() file and serve reads from it.
// ---------------------------------------------------------------------------

DiskIndex::DiskIndex(const std::string& path) {
    fd_ = ::open(path.c_str(), O_RDONLY);
    if (fd_ < 0) throw std::runtime_error("cannot open index file: " + path);

    struct stat st{};
    if (::fstat(fd_, &st) != 0) {
        ::close(fd_);
        throw std::runtime_error("cannot stat index file: " + path);
    }
    map_size_ = static_cast<std::size_t>(st.st_size);
    if (map_size_ < 24) {
        ::close(fd_);
        throw std::runtime_error("index file too small / corrupt: " + path);
    }

    void* m = ::mmap(nullptr, map_size_, PROT_READ, MAP_PRIVATE, fd_, 0);
    if (m == MAP_FAILED) {
        ::close(fd_);
        throw std::runtime_error("mmap failed for index file: " + path);
    }
    base_ = static_cast<const uint8_t*>(m);

    // --- header --- (memcpy avoids any unaligned-read worries)
    if (std::memcmp(base_, "JOHOIX01", 8) != 0)
        throw std::runtime_error("bad magic (not a Joho index): " + path);
    uint32_t num_terms = 0;
    std::memcpy(&num_docs_, base_ + 8, sizeof(num_docs_));
    std::memcpy(&num_terms, base_ + 12, sizeof(num_terms));
    std::memcpy(&avgdl_, base_ + 16, sizeof(avgdl_));

    // --- doc lengths: a uint32 array we index into directly (no copy) ---
    doc_lens_ = base_ + 24;
    std::size_t pos = 24 + static_cast<std::size_t>(num_docs_) * 4;

    // --- external ids: decode the length-prefixed strings into RAM ---
    ext_ids_.reserve(num_docs_);
    for (uint32_t i = 0; i < num_docs_; ++i) {
        const uint32_t id_len = get_varint(base_, pos);
        ext_ids_.emplace_back(reinterpret_cast<const char*>(base_ + pos), id_len);
        pos += id_len;
    }

    // --- dictionary: record where each term's postings live; leave the postings
    //     bytes on disk (mmapped), to be decoded lazily by postings(). ---
    dict_.reserve(num_terms);
    for (uint32_t t = 0; t < num_terms; ++t) {
        const uint32_t term_len = get_varint(base_, pos);
        std::string term(reinterpret_cast<const char*>(base_ + pos), term_len);
        pos += term_len;
        const uint32_t df = get_varint(base_, pos);
        const uint32_t plen = get_varint(base_, pos);
        dict_.emplace(std::move(term), TermEntry{base_ + pos, plen, df});
        pos += plen;
    }
}

DiskIndex::~DiskIndex() {
    if (base_ != nullptr) ::munmap(const_cast<uint8_t*>(base_), map_size_);
    if (fd_ >= 0) ::close(fd_);
}

uint32_t DiskIndex::doc_len(uint32_t doc_id) const {
    uint32_t v = 0;
    std::memcpy(&v, doc_lens_ + static_cast<std::size_t>(doc_id) * 4, sizeof(v));
    return v;
}

std::size_t DiskIndex::df(const std::string& term) const {
    auto it = dict_.find(term);
    return it == dict_.end() ? 0 : it->second.df;
}

void DiskIndex::postings(const std::string& term, std::vector<Posting>& out) const {
    out.clear();
    auto it = dict_.find(term);
    if (it == dict_.end()) return;

    // Reverse of the writer: read gap + tf varints, undo the delta encoding.
    const uint8_t* p = it->second.ptr;
    const uint32_t len = it->second.len;
    out.reserve(it->second.df);
    std::size_t pos = 0;
    uint32_t prev = 0;
    while (pos < len) {
        const uint32_t gap = get_varint(p, pos);
        const uint32_t tf = get_varint(p, pos);
        prev += gap;  // absolute doc id = running sum of gaps
        out.push_back(Posting{prev, tf});
    }
}

}  // namespace joho
