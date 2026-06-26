// Tests for SymSpell spell correction + the Damerau OSA distance
// (engine/include/joho/symspell.hpp). No framework; ctest checks the exit code.

#include <cstdio>
#include <string>

#include "joho/symspell.hpp"

namespace {

int g_failures = 0;

void check(bool cond, const char* what) {
    if (!cond) { std::printf("  FAIL: %s\n", what); ++g_failures; }
}

}  // namespace

int main() {
    using joho::damerau_osa_distance;

    // --- edit distance ---
    check(damerau_osa_distance("", "") == 0, "empty vs empty = 0");
    check(damerau_osa_distance("abc", "abc") == 0, "identical = 0");
    check(damerau_osa_distance("abc", "ab") == 1, "one deletion");
    check(damerau_osa_distance("ab", "abc") == 1, "one insertion");
    check(damerau_osa_distance("abc", "abd") == 1, "one substitution");
    check(damerau_osa_distance("teh", "the") == 1, "transposition counts as one");
    check(damerau_osa_distance("kitten", "sitting") == 3, "classic kitten/sitting = 3");

    // --- SymSpell lookup ---
    joho::SymSpell sym(2);
    sym.add("science", 90);
    sym.add("scientist", 30);
    sym.add("machine", 100);
    sym.add("learning", 80);
    sym.add("the", 1000);

    // Exact match -> distance 0.
    {
        const auto s = sym.lookup("machine");
        check(s.found && s.distance == 0 && s.term == "machine", "exact match is distance 0");
    }
    // One substitution.
    {
        const auto s = sym.lookup("machkne");
        check(s.found && s.term == "machine" && s.distance == 1, "machkne -> machine (dist 1)");
    }
    // One deletion in the query (missing letter).
    {
        const auto s = sym.lookup("machne");
        check(s.found && s.term == "machine", "machne -> machine");
    }
    // Transposition.
    {
        const auto s = sym.lookup("teh");
        check(s.found && s.term == "the" && s.distance == 1, "teh -> the (transposition)");
    }
    // Beyond max edit distance -> not found.
    {
        const auto s = sym.lookup("zzzzzz");
        check(!s.found, "garbage has no correction within distance 2");
    }
    // Tie on distance broken by frequency: a 1-edit word near both "science"(90)
    // and "scientist"(30) should prefer the more frequent "science".
    {
        const auto s = sym.lookup("science");  // delete one 'e' region -> science(d1)
        check(s.found && s.term == "science", "ties prefer higher frequency");
    }

    if (g_failures == 0) { std::printf("symspell: ALL TESTS PASSED\n"); return 0; }
    std::printf("symspell: %d FAILURE(S)\n", g_failures);
    return 1;
}
