#include "joho/symspell.hpp"

#include <algorithm>
#include <unordered_set>

namespace joho {

int damerau_osa_distance(const std::string& a, const std::string& b) {
    const std::size_t n = a.size(), m = b.size();
    if (n == 0) return static_cast<int>(m);
    if (m == 0) return static_cast<int>(n);

    // Classic DP table, plus the one extra case (transposition) that turns plain
    // Levenshtein into Damerau "optimal string alignment".
    std::vector<std::vector<int>> d(n + 1, std::vector<int>(m + 1, 0));
    for (std::size_t i = 0; i <= n; ++i) d[i][0] = static_cast<int>(i);
    for (std::size_t j = 0; j <= m; ++j) d[0][j] = static_cast<int>(j);

    for (std::size_t i = 1; i <= n; ++i) {
        for (std::size_t j = 1; j <= m; ++j) {
            const int cost = (a[i - 1] == b[j - 1]) ? 0 : 1;
            d[i][j] = std::min({
                d[i - 1][j] + 1,         // deletion
                d[i][j - 1] + 1,         // insertion
                d[i - 1][j - 1] + cost,  // substitution / match
            });
            if (i > 1 && j > 1 && a[i - 1] == b[j - 2] && a[i - 2] == b[j - 1]) {
                d[i][j] = std::min(d[i][j], d[i - 2][j - 2] + 1);  // transposition
            }
        }
    }
    return d[n][m];
}

std::vector<std::string> SymSpell::deletes_within(const std::string& word) const {
    // Breadth-expand: start from the word, repeatedly delete one char, up to
    // max_edit_ times. A set de-duplicates the many paths that reach the same string.
    std::unordered_set<std::string> seen;
    std::vector<std::string> frontier{word};
    seen.insert(word);
    for (int edit = 0; edit < max_edit_; ++edit) {
        std::vector<std::string> next;
        for (const std::string& w : frontier) {
            if (w.size() <= 1) continue;  // don't delete down to the empty string
            for (std::size_t i = 0; i < w.size(); ++i) {
                std::string del = w.substr(0, i) + w.substr(i + 1);
                if (seen.insert(del).second) next.push_back(del);
            }
        }
        frontier.swap(next);
    }
    return std::vector<std::string>(seen.begin(), seen.end());
}

void SymSpell::add(const std::string& term, uint64_t frequency) {
    if (term.empty() || frequency == 0) return;
    uint64_t& f = freq_[term];
    const bool is_new = (f == 0);
    f += frequency;
    if (!is_new) return;  // delete variants already indexed for this term
    for (const std::string& del : deletes_within(term)) {
        delete_index_[del].push_back(term);
    }
}

std::vector<Suggestion> SymSpell::lookup_all(const std::string& word, std::size_t k) const {
    std::vector<Suggestion> out;
    if (word.empty()) return out;

    // Gather candidate dictionary terms: any term that shares a delete-variant with
    // the query word (this is the SymSpell probe — no scan of the dictionary).
    std::unordered_set<std::string> candidates;
    for (const std::string& del : deletes_within(word)) {
        if (freq_.count(del)) candidates.insert(del);  // del is itself a real term
        const auto it = delete_index_.find(del);
        if (it != delete_index_.end()) {
            for (const std::string& term : it->second) candidates.insert(term);
        }
    }

    // Verify true edit distance and keep those within budget.
    for (const std::string& cand : candidates) {
        const int dist = damerau_osa_distance(word, cand);
        if (dist <= max_edit_) {
            out.push_back({cand, dist, freq_.at(cand), true});
        }
    }
    // Rank: closer first, then more frequent, then alphabetical for determinism.
    std::sort(out.begin(), out.end(), [](const Suggestion& a, const Suggestion& b) {
        if (a.distance != b.distance) return a.distance < b.distance;
        if (a.frequency != b.frequency) return a.frequency > b.frequency;
        return a.term < b.term;
    });
    if (out.size() > k) out.resize(k);
    return out;
}

Suggestion SymSpell::lookup(const std::string& word) const {
    std::vector<Suggestion> all = lookup_all(word, 1);
    return all.empty() ? Suggestion{} : all.front();
}

}  // namespace joho
