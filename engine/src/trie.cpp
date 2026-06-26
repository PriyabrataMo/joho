#include "joho/trie.hpp"

#include <algorithm>
#include <queue>

namespace joho {

void Trie::insert(const std::string& term, uint64_t weight) {
    if (term.empty() || weight == 0) return;
    Node* node = &root_;
    root_.max_weight = std::max(root_.max_weight, weight);
    for (const char ch : term) {
        std::unique_ptr<Node>& slot = node->kids[ch];
        if (!slot) slot = std::make_unique<Node>();
        node = slot.get();
        // Every node on the path now has at least this weight beneath it.
        node->max_weight = std::max(node->max_weight, weight);
    }
    if (node->weight == 0) ++num_terms_;  // first time this exact term ends here
    node->weight += weight;
}

const Trie::Node* Trie::descend(const std::string& prefix) const {
    const Node* node = &root_;
    for (const char ch : prefix) {
        const auto it = node->kids.find(ch);
        if (it == node->kids.end()) return nullptr;
        node = it->second.get();
    }
    return node;
}

std::vector<Completion> Trie::complete(const std::string& prefix, std::size_t k) const {
    std::vector<Completion> out;
    if (k == 0) return out;
    const Node* start = descend(prefix);
    if (!start) return out;

    // Best-first search. The frontier holds two kinds of entries, both keyed by an
    // upper bound on the weight they can yield, so popping the max key is always safe:
    //   * a NODE entry  -> a subtree still to expand; key = its max_weight
    //   * a WORD entry   -> a concrete completion ready to emit; key = its exact weight
    // When a WORD reaches the top of the heap, its weight is >= every remaining key,
    // so it is the next-largest completion overall — emit it. This yields results in
    // descending weight without ever materializing the full subtree.
    struct Entry {
        uint64_t key;
        bool is_word;
        std::string str;     // full term (prefix + path walked so far)
        const Node* node;    // valid only when !is_word
        bool operator<(const Entry& o) const { return key < o.key; }  // max-heap
    };
    std::priority_queue<Entry> pq;
    pq.push({start->max_weight, false, prefix, start});

    while (!pq.empty() && out.size() < k) {
        Entry e = pq.top();
        pq.pop();
        if (e.is_word) {
            out.push_back({std::move(e.str), e.key});
            continue;
        }
        // Expand the subtree: the node itself may be a term, and each child is a
        // smaller subtree bounded by its own max_weight.
        if (e.node->weight > 0) {
            pq.push({e.node->weight, true, e.str, nullptr});
        }
        for (const auto& [ch, child] : e.node->kids) {
            pq.push({child->max_weight, false, e.str + ch, child.get()});
        }
    }
    return out;
}

}  // namespace joho
