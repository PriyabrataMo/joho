#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace joho {

// One autocomplete suggestion: a completed term and its "weight" (how popular the
// term is — here we use its corpus frequency as the popularity proxy).
struct Completion {
    std::string term;
    uint64_t weight;
};

// A prefix trie ("re-trie-val tree") for query autocomplete (P5).
//
// A trie stores strings by sharing common prefixes: the path from the root spells
// out a string, and a node is "terminal" if some inserted term ends there. So all
// terms that start with "sci" hang under the single path s -> c -> i. To autocomplete
// a prefix we walk to that node, then enumerate the terms in the subtree below it.
//
// The twist that makes this *good* autocomplete (not just correct) is ranking: we
// don't want any 8 completions of "sci", we want the 8 most *popular* ones. Each
// node caches `max_weight` = the largest term-weight anywhere in its subtree. That
// lets complete() run a **best-first (branch-and-bound) search**: a max-heap keyed
// on `max_weight` always expands the most promising branch next, so completions
// pop out in strictly descending weight order and we can stop after k — without
// visiting the whole subtree.
//
// Why a trie and not a hashmap? A hashmap can't answer "all keys with this prefix"
// without scanning every key. The trie makes prefix the *primary* access path.
//
// (Production engines often compact this into an **FST / DAWG**, which also shares
// common *suffixes* to shrink the structure. A plain
// trie is the clearest correct version and is what we measure here.)
class Trie {
public:
    // Insert `term` with the given weight. Inserting the same term twice adds the
    // weights (so you can stream term occurrences and let them accumulate).
    void insert(const std::string& term, uint64_t weight = 1);

    // The top-`k` completions of `prefix`, highest weight first. If `prefix` is
    // itself a stored term it is included. Returns fewer than k if the subtree has
    // fewer terms; empty if no term has this prefix.
    std::vector<Completion> complete(const std::string& prefix, std::size_t k = 8) const;

    std::size_t num_terms() const { return num_terms_; }

private:
    struct Node {
        std::unordered_map<char, std::unique_ptr<Node>> kids;
        uint64_t weight = 0;      // >0 iff a term ends exactly here
        uint64_t max_weight = 0;  // max term-weight in this whole subtree (for pruning)
    };

    // Walk to the node spelling `prefix`, or nullptr if no such path exists.
    const Node* descend(const std::string& prefix) const;

    Node root_;
    std::size_t num_terms_ = 0;
};

}  // namespace joho
