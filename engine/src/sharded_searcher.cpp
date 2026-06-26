#include "joho/sharded_searcher.hpp"

#include <algorithm>
#include <thread>

namespace joho {

ShardedSearcher::ShardedSearcher(const std::vector<std::string>& shard_paths, double k1, double b) {
    shards_.reserve(shard_paths.size());
    for (const std::string& path : shard_paths) {
        Shard s;
        s.index = std::make_unique<DiskIndex>(path);   // throws if the file is bad
        s.bm25 = std::make_unique<BM25>(*s.index, k1, b);
        shards_.push_back(std::move(s));
    }
}

std::size_t ShardedSearcher::total_docs() const {
    std::size_t n = 0;
    for (const Shard& s : shards_) n += s.index->num_docs();
    return n;
}

std::vector<ScoredDoc> ShardedSearcher::search(const std::string& query, std::size_t top_k,
                                               std::size_t per_shard_k) const {
    if (shards_.empty()) return {};
    // Each leaf returns at least top_k so the merge has enough candidates; the
    // global top-k can in the worst case come entirely from one shard.
    if (per_shard_k == 0) per_shard_k = top_k;

    // --- scatter: one thread per shard, each scoring independently ---
    std::vector<std::vector<ScoredDoc>> leaf(shards_.size());
    std::vector<std::thread> workers;
    workers.reserve(shards_.size());
    for (std::size_t i = 0; i < shards_.size(); ++i) {
        workers.emplace_back([&, i] { leaf[i] = shards_[i].bm25->search(query, per_shard_k); });
    }
    for (std::thread& t : workers) t.join();

    // --- gather: merge all leaf results, keep the global top-k by score ---
    std::vector<ScoredDoc> merged;
    for (auto& l : leaf) {
        merged.insert(merged.end(), std::make_move_iterator(l.begin()),
                      std::make_move_iterator(l.end()));
    }
    // Partial sort is enough — we only need the top_k in order. Ties broken by id so
    // the run is deterministic regardless of thread scheduling.
    const std::size_t keep = std::min(top_k, merged.size());
    std::partial_sort(merged.begin(), merged.begin() + keep, merged.end(),
                      [](const ScoredDoc& a, const ScoredDoc& b) {
                          if (a.score != b.score) return a.score > b.score;
                          return a.external_id < b.external_id;
                      });
    merged.resize(keep);
    return merged;
}

}  // namespace joho
