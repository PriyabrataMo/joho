#pragma once
#include <string>
#include <vector>

namespace joho {

// Turns raw text into a list of normalized "tokens" (words).
//
// Normalization here is deliberately simple:
//   * everything is lowercased
//   * a token is a maximal run of letters/digits [a-z0-9]
//   * any other character (space, punctuation) is a separator
//
// We do NOT stem ("running" -> "run") or remove stopwords ("the", "is") yet.
// We add those later and *measure* whether they actually improve the metrics,
// rather than assuming they help.
class Tokenizer {
public:
    std::vector<std::string> tokenize(const std::string& text) const;
};

}  // namespace joho
