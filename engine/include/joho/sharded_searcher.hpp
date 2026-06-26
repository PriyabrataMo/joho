#pragma once
#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include "joho/bm25.hpp"
#include "joho/disk_index.hpp"

namespace joho {

// Scatter-gather search over a **sharded** index (P7).
//
// One machine can't hold a web-scale index, so real engines split the corpus into
// N shards, each on its own server. A query is *scattered* to all shards, each
// returns its local top-k, and the leaf results are *gathered* and merged into one
// global ranking. This is the standard architecture large-scale engines use at the leaf.
//
// Here we run all shards in one process (one std::thread per shard, since each
// DiskIndex is independent and read-only) — same algorithm, single box, so we can
// measure it. The on-disk shards are produced by `joho_build --shards N`.
//
// **The honest caveat:** BM25 needs corpus statistics — idf (how rare a term is) and
// avgdl (average doc length). With independent shards, each scores using its *local*
// statistics, so a term that's rare globally but common in one shard gets a different
// idf there. The merged ranking therefore *approximates* the single-index ranking
// rather than reproducing it exactly; we quantify that gap by comparing sharded vs
// single-index top-k. The production fix is a stats-broadcast phase (gather global
// df/avgdl first, then score); we keep local stats here to expose the trade-off
// rather than hide it.
class ShardedSearcher {
public:
    // Open `shards.size()` on-disk indexes and attach a BM25 scorer to each.
    ShardedSearcher(const std::vector<std::string>& shard_paths, double k1 = 0.9, double b = 0.4);

    // Scatter the query to every shard, gather their top-k, merge to a global top-k.
    // `per_shard_k` is how many each leaf returns before the merge (>= top_k; a
    // larger value improves merge quality at the cost of more leaf work).
    std::vector<ScoredDoc> search(const std::string& query, std::size_t top_k = 1000,
                                  std::size_t per_shard_k = 0) const;

    std::size_t num_shards() const { return shards_.size(); }
    std::size_t total_docs() const;

private:
    struct Shard {
        std::unique_ptr<DiskIndex> index;
        std::unique_ptr<BM25> bm25;
    };
    std::vector<Shard> shards_;
};

}  // namespace joho
