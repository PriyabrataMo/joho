#include "joho/suggest_index.hpp"

#include <fstream>
#include <iostream>
#include <unordered_map>

#include "joho/corpus.hpp"
#include "joho/tokenizer.hpp"

namespace joho {

void SuggestIndex::add_term(const std::string& term, uint64_t count) {
    if (term.empty() || count == 0) return;
    trie_.insert(term, count);
    sym_.add(term, count);
}

std::size_t SuggestIndex::build_from_corpus(const std::string& corpus_path) {
    std::ifstream in(corpus_path);
    if (!in) {
        std::cerr << "error: cannot open corpus '" << corpus_path << "'\n";
        return 0;
    }

    // First pass: count term frequencies across the whole corpus. We aggregate into
    // a map and only *then* insert, so each distinct term is added once with its
    // total count (SymSpell's delete-index generation is per distinct term, not per
    // occurrence — important for build speed).
    Tokenizer tok;
    std::unordered_map<std::string, uint64_t> freq;
    std::string line, id, text;
    std::size_t n_docs = 0;
    while (std::getline(in, line)) {
        if (!split_first_tab(line, id, text)) continue;
        for (const std::string& term : tok.tokenize(text)) ++freq[term];
        if (++n_docs % 100000 == 0) std::cerr << "  ... " << n_docs << " docs\r";
    }

    for (const auto& [term, count] : freq) add_term(term, count);
    std::cerr << "SuggestIndex: " << freq.size() << " distinct terms from " << n_docs
              << " docs\n";
    return freq.size();
}

}  // namespace joho
