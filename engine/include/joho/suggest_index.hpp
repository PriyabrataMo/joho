#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "joho/symspell.hpp"
#include "joho/trie.hpp"

namespace joho {

// Bundles the two query-assist structures (P5) behind one builder, so the CLI
// (`joho_suggest`) and the gRPC server build them the same way: tokenize a corpus,
// count how often each term occurs, then feed those (term, frequency) pairs into
//   * a Trie    — prefix autocomplete, ranked by frequency
//   * a SymSpell — "did you mean?" correction, ranked by (distance, frequency)
//
// Using corpus frequency as the weight means both features favor terms users are
// actually likely to want — common terms autocomplete first and are the preferred
// spelling target. (A production system would blend in *query-log* frequency, which
// reflects demand rather than supply.)
class SuggestIndex {
public:
    explicit SuggestIndex(int max_edit_distance = 2) : sym_(max_edit_distance) {}

    // Tokenize every document in a corpus TSV (<id>\t<text>) and accumulate term
    // frequencies into the trie and the corrector. Returns the number of distinct
    // terms learned, or 0 if the file could not be opened.
    std::size_t build_from_corpus(const std::string& corpus_path);

    // Add a single observed term occurrence (or `count` of them). Exposed so other
    // sources (query logs, a prebuilt vocabulary file) can contribute too.
    void add_term(const std::string& term, uint64_t count = 1);

    std::vector<Completion> complete(const std::string& prefix, std::size_t k = 8) const {
        return trie_.complete(prefix, k);
    }
    Suggestion correct(const std::string& word) const { return sym_.lookup(word); }
    std::vector<Suggestion> correct_all(const std::string& word, std::size_t k = 5) const {
        return sym_.lookup_all(word, k);
    }

    std::size_t vocab_size() const { return trie_.num_terms(); }

private:
    Trie trie_;
    SymSpell sym_;
};

}  // namespace joho
