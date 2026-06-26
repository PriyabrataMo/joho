// Tests for the autocomplete trie (engine/include/joho/trie.hpp).
//
// No test framework (same convention as test_varint.cpp): assert, summarize,
// return non-zero on failure so ctest catches it.

#include <cstdio>
#include <string>
#include <vector>

#include "joho/trie.hpp"

namespace {

int g_failures = 0;

void check(bool cond, const char* what) {
    if (!cond) { std::printf("  FAIL: %s\n", what); ++g_failures; }
}

}  // namespace

int main() {
    joho::Trie t;
    t.insert("science", 50);
    t.insert("scientist", 30);
    t.insert("scientific", 90);
    t.insert("scope", 10);
    t.insert("search", 100);

    check(t.num_terms() == 5, "five distinct terms inserted");

    // Prefix "sci" -> the three sci* terms, returned in DESCENDING weight order.
    {
        const auto cs = t.complete("sci", 8);
        check(cs.size() == 3, "three completions for 'sci'");
        check(cs[0].term == "scientific" && cs[0].weight == 90, "top is highest-weight (scientific)");
        check(cs[1].term == "science", "second is science");
        check(cs[2].term == "scientist", "third is scientist");
    }

    // top-k truncation respects the ranking.
    {
        const auto cs = t.complete("sci", 2);
        check(cs.size() == 2, "k=2 returns two");
        check(cs[0].term == "scientific" && cs[1].term == "science", "k=2 keeps the top two by weight");
    }

    // A prefix equal to a full term includes that term.
    {
        const auto cs = t.complete("scope", 8);
        check(cs.size() == 1 && cs[0].term == "scope", "exact-term prefix returns the term itself");
    }

    // Unknown prefix -> empty.
    check(t.complete("zzz", 8).empty(), "unknown prefix returns no completions");

    // Empty prefix -> everything, still weight-ordered (search=100 is the global max).
    {
        const auto cs = t.complete("", 8);
        check(cs.size() == 5, "empty prefix returns all terms");
        check(cs[0].term == "search", "global top is the highest-weight term");
    }

    // Re-inserting accumulates weight rather than duplicating the term.
    t.insert("scope", 200);
    {
        const auto cs = t.complete("scope", 8);
        check(cs.size() == 1 && cs[0].weight == 210, "re-insert accumulates weight");
        check(t.num_terms() == 5, "term count unchanged after re-insert");
    }

    if (g_failures == 0) { std::printf("trie: ALL TESTS PASSED\n"); return 0; }
    std::printf("trie: %d FAILURE(S)\n", g_failures);
    return 1;
}
