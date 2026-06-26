#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace joho {

// The outcome of a spell-correction lookup.
struct Suggestion {
    std::string term;       // the suggested (dictionary) term
    int distance = 0;       // edit distance from the query word (0 = exact match)
    uint64_t frequency = 0; // corpus frequency of the suggested term
    bool found = false;     // false => no dictionary term within max edit distance
};

// "Did you mean?" spell correction via the **SymSpell** algorithm (P5).
//
// The naive way to find dictionary words within edit distance d of a typo is to
// compare the typo against every dictionary word — far too slow. SymSpell's trick:
// at index time, for every dictionary word, also store every string you get by
// *deleting* up to d characters ("delete variants"), all pointing back to the word.
//
// The insight is that the four edit operations are symmetric under deletion:
//   * a DELETE in the query        == an insert in the dictionary word
//   * an INSERT in the query        == a delete in the query  (delete it back out)
//   * a REPLACE                     == one delete on each side meeting in the middle
//   * a TRANSPOSE                   == reachable via deletes too
// So at lookup we only need to generate the *deletes of the query word* (cheap —
// no alphabet involved) and probe the precomputed delete index. Any dictionary word
// whose own delete-set overlaps the query's delete-set is a candidate; we then
// verify the true edit distance and rank.
//
// Cost: deletes-only generation is O(word_len choose d) per side — tiny for real
// words — instead of O(dictionary_size). This is what makes correction sub-millisecond.
class SymSpell {
public:
    explicit SymSpell(int max_edit_distance = 2) : max_edit_(max_edit_distance) {}

    // Add a dictionary term with its frequency (popularity). Repeated adds sum.
    void add(const std::string& term, uint64_t frequency = 1);

    // Best correction for `word`: an exact match if the word is in the dictionary,
    // else the closest term (ties broken by higher frequency). `found` is false if
    // nothing lies within the max edit distance.
    Suggestion lookup(const std::string& word) const;

    // Top-`k` candidate corrections, ranked by (distance asc, frequency desc).
    std::vector<Suggestion> lookup_all(const std::string& word, std::size_t k = 5) const;

    std::size_t num_terms() const { return freq_.size(); }
    int max_edit_distance() const { return max_edit_; }

private:
    // All strings obtainable by deleting up to max_edit_ chars from `word` (incl.
    // the word itself), de-duplicated.
    std::vector<std::string> deletes_within(const std::string& word) const;

    int max_edit_;
    std::unordered_map<std::string, uint64_t> freq_;  // term -> frequency (the dictionary)
    // delete-variant -> dictionary terms that produce it. The index SymSpell probes.
    std::unordered_map<std::string, std::vector<std::string>> delete_index_;
};

// Damerau–Levenshtein "optimal string alignment" distance: insertions, deletions,
// substitutions, and adjacent transpositions ("teh"->"the" costs 1). Standalone so
// it can be unit-tested directly.
int damerau_osa_distance(const std::string& a, const std::string& b);

}  // namespace joho
