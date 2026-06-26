#include "joho/corpus.hpp"

#include <chrono>
#include <fstream>
#include <iostream>

namespace joho {

bool split_first_tab(const std::string& line, std::string& id, std::string& text) {
    const std::size_t tab = line.find('\t');
    if (tab == std::string::npos) return false;
    id = line.substr(0, tab);
    text = line.substr(tab + 1);
    if (!text.empty() && text.back() == '\r') text.pop_back();
    return !id.empty();
}

std::size_t load_corpus_tsv(const std::string& path, InvertedIndex& index) {
    std::ifstream in(path);
    if (!in) {
        std::cerr << "error: cannot open corpus '" << path << "'\n";
        return 0;
    }
    const auto start = std::chrono::steady_clock::now();
    auto elapsed = [&] {
        return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    };

    std::size_t n = 0;
    std::string line, id, text;
    while (std::getline(in, line)) {
        if (!split_first_tab(line, id, text)) continue;
        index.add_document(id, text);
        if (++n % 100000 == 0) {
            std::cerr << "  indexed " << n << " docs (" << elapsed() << "s)\n";
        }
    }
    index.finalize();
    std::cerr << "Indexed " << n << " documents in " << elapsed()
              << "s; avgdl=" << index.avgdl() << "\n";
    return n;
}

}  // namespace joho
