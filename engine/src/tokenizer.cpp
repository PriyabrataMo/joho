#include "joho/tokenizer.hpp"

#include <cctype>

namespace joho {

std::vector<std::string> Tokenizer::tokenize(const std::string& text) const {
    std::vector<std::string> tokens;
    std::string current;
    current.reserve(32);

    for (unsigned char ch : text) {
        if (std::isalnum(ch)) {
            // Part of a word: lowercase it and keep building the current token.
            current.push_back(static_cast<char>(std::tolower(ch)));
        } else if (!current.empty()) {
            // Hit a separator and we have a word in progress -> emit it.
            tokens.push_back(current);
            current.clear();
        }
    }
    if (!current.empty()) tokens.push_back(current);  // last word, if any

    return tokens;
}

}  // namespace joho
