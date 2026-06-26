// Joho engine — autocomplete + spell-correction driver (P5).
//
// Builds a vocabulary from a corpus (term -> corpus frequency) and serves two
// query-assist features over it:
//   * autocomplete  — top-k completions of a prefix, ranked by popularity (Trie)
//   * "did you mean" — closest dictionary term to a typo, within edit distance (SymSpell)
//
// Modes:
//   ./joho_suggest --corpus corpus.tsv --complete "sci"      # one-shot completion
//   ./joho_suggest --corpus corpus.tsv --correct  "machne"   # one-shot correction
//   ./joho_suggest --corpus corpus.tsv --eval-spell pairs.tsv# accuracy on typo pairs
//   ./joho_suggest --corpus corpus.tsv                        # interactive REPL
//
// The REPL: type a word -> if it's a known term we autocomplete it, otherwise we
// suggest a correction. A trailing '*' forces completion; a leading '?' forces
// correction.

#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>

#include "joho/corpus.hpp"
#include "joho/suggest_index.hpp"

namespace {

struct Args {
    std::string corpus;
    std::string complete;
    std::string correct;
    std::string eval_spell;
    std::size_t top_k = 8;
    int max_edit = 2;
    bool interactive = false;
};

void usage(const char* a0) {
    std::cerr
        << "Usage: " << a0 << " --corpus FILE [mode]\n"
        << "  --corpus FILE        corpus TSV <id>\\t<text> to learn the vocabulary from (required)\n"
        << "  --complete PREFIX    print top-k completions of PREFIX and exit\n"
        << "  --correct  WORD      print the best correction of WORD and exit\n"
        << "  --eval-spell FILE    TSV <typo>\\t<correct>; report top-1 correction accuracy\n"
        << "  --top-k N            completions/candidates to show   (default 8)\n"
        << "  --max-edit N         max edit distance for correction (default 2)\n"
        << "  (no mode)            interactive REPL\n";
}

bool parse(int argc, char** argv, Args& a) {
    for (int i = 1; i < argc; ++i) {
        const std::string s = argv[i];
        auto next = [&]() -> std::string {
            return (i + 1 < argc) ? std::string(argv[++i]) : std::string();
        };
        if (s == "--corpus") a.corpus = next();
        else if (s == "--complete") a.complete = next();
        else if (s == "--correct") a.correct = next();
        else if (s == "--eval-spell") a.eval_spell = next();
        else if (s == "--top-k") a.top_k = std::stoul(next());
        else if (s == "--max-edit") a.max_edit = std::stoi(next());
        else if (s == "-h" || s == "--help") { usage(argv[0]); std::exit(0); }
        else { std::cerr << "error: unknown argument '" << s << "'\n"; return false; }
    }
    if (a.corpus.empty()) { std::cerr << "error: --corpus is required\n\n"; usage(argv[0]); return false; }
    return true;
}

void print_completions(const joho::SuggestIndex& idx, const std::string& prefix, std::size_t k) {
    const auto cs = idx.complete(prefix, k);
    if (cs.empty()) { std::cout << "  (no completions for '" << prefix << "')\n"; return; }
    for (const auto& c : cs) std::cout << "  " << c.term << "  (" << c.weight << ")\n";
}

void print_correction(const joho::SuggestIndex& idx, const std::string& word, std::size_t k) {
    const auto s = idx.correct(word);
    if (!s.found) { std::cout << "  (no correction within edit distance for '" << word << "')\n"; return; }
    if (s.distance == 0) { std::cout << "  '" << word << "' is spelled correctly\n"; return; }
    std::cout << "  did you mean: " << s.term << "  (dist " << s.distance
              << ", freq " << s.frequency << ")\n";
    const auto all = idx.correct_all(word, k);
    if (all.size() > 1) {
        std::cout << "  others:";
        for (std::size_t i = 1; i < all.size(); ++i) std::cout << ' ' << all[i].term;
        std::cout << "\n";
    }
}

// Read a TSV of <typo>\t<expected> and report how often the top-1 correction matches.
int eval_spell(const joho::SuggestIndex& idx, const std::string& path) {
    std::ifstream in(path);
    if (!in) { std::cerr << "error: cannot open '" << path << "'\n"; return 1; }
    std::size_t total = 0, correct = 0;
    std::string line, typo, expected;
    while (std::getline(in, line)) {
        if (!joho::split_first_tab(line, typo, expected)) continue;
        ++total;
        const auto s = idx.correct(typo);
        const bool ok = s.found && s.term == expected;
        if (ok) ++correct;
        std::cout << (ok ? "  ok   " : "  MISS ") << typo << " -> "
                  << (s.found ? s.term : std::string("(none)")) << "  (want " << expected << ")\n";
    }
    if (total == 0) { std::cerr << "error: no valid pairs in '" << path << "'\n"; return 1; }
    std::cout << "\nspell top-1 accuracy: " << correct << "/" << total << "  ("
              << (100.0 * static_cast<double>(correct) / static_cast<double>(total)) << "%)\n";
    return 0;
}

void repl(const joho::SuggestIndex& idx, std::size_t k) {
    std::cout << "Interactive query-assist. '<prefix>*' forces completion, '?<word>' forces\n"
                 "correction; otherwise: known term -> complete, unknown -> correct. Ctrl-D quits.\n";
    std::string word;
    while (std::cout << "> " && std::getline(std::cin, word)) {
        if (word.empty()) continue;
        if (word.back() == '*') { print_completions(idx, word.substr(0, word.size() - 1), k); continue; }
        if (word.front() == '?') { print_correction(idx, word.substr(1), k); continue; }
        // Heuristic: if the word is a known term (distance-0 correction), complete it;
        // otherwise offer a correction.
        const auto s = idx.correct(word);
        if (s.found && s.distance == 0) print_completions(idx, word, k);
        else print_correction(idx, word, k);
    }
    std::cout << "\n";
}

}  // namespace

int main(int argc, char** argv) {
    Args args;
    if (!parse(argc, argv, args)) return 1;

    joho::SuggestIndex idx(args.max_edit);
    const auto t0 = std::chrono::steady_clock::now();
    if (idx.build_from_corpus(args.corpus) == 0) return 1;
    const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    std::cerr << "Built query-assist index in " << secs << "s ("
              << idx.vocab_size() << " terms)\n";

    if (!args.complete.empty()) { print_completions(idx, args.complete, args.top_k); return 0; }
    if (!args.correct.empty())  { print_correction(idx, args.correct, args.top_k); return 0; }
    if (!args.eval_spell.empty()) return eval_spell(idx, args.eval_spell);
    repl(idx, args.top_k);
    return 0;
}
