// Joho engine — offline index builder (P2, extended for sharding in P7).
//
// Reads a corpus TSV, builds the inverted index, and serializes it to a compact
// on-disk index. Prints a raw-vs-compressed size report (the P2 deliverable).
//
//   ./joho_build --corpus corpus.tsv --output index.joho            # single index
//   ./joho_build --corpus corpus.tsv --output idx --shards 4        # idx.0.joho ... idx.3.joho
//
// With --shards N (>1), --output is treated as a PREFIX and the corpus is split
// across N shards by hashing each document's external id — the input side of the
// scatter-gather search in sharded_searcher.hpp (P7).

#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "joho/corpus.hpp"
#include "joho/disk_index.hpp"
#include "joho/inverted_index.hpp"

namespace {

struct Args {
    std::string corpus;
    std::string output;
    int shards = 1;
};

bool parse(int argc, char** argv, Args& a) {
    for (int i = 1; i < argc; ++i) {
        const std::string s = argv[i];
        auto next = [&]() -> std::string {
            return (i + 1 < argc) ? std::string(argv[++i]) : std::string();
        };
        if (s == "--corpus") a.corpus = next();
        else if (s == "--output" || s == "-o") a.output = next();
        else if (s == "--shards") a.shards = std::stoi(next());
        else if (s == "-h" || s == "--help") {
            std::cerr << "Usage: joho_build --corpus FILE --output FILE [--shards N]\n";
            std::exit(0);
        } else {
            std::cerr << "error: unknown argument '" << s << "'\n";
            return false;
        }
    }
    if (a.corpus.empty() || a.output.empty()) {
        std::cerr << "error: --corpus and --output are required\n";
        return false;
    }
    if (a.shards < 1) { std::cerr << "error: --shards must be >= 1\n"; return false; }
    return true;
}

double mib(uint64_t bytes) { return static_cast<double>(bytes) / (1024.0 * 1024.0); }

void print_stats(const std::string& path, const joho::IndexStats& st) {
    const double ratio = st.compressed_postings_bytes
        ? static_cast<double>(st.raw_postings_bytes) /
              static_cast<double>(st.compressed_postings_bytes)
        : 0.0;
    const double pct = st.raw_postings_bytes
        ? 100.0 * static_cast<double>(st.compressed_postings_bytes) /
              static_cast<double>(st.raw_postings_bytes)
        : 0.0;
    std::cout << std::fixed << std::setprecision(2);
    std::cout << "Wrote on-disk index: " << path << "\n";
    std::cout << "  documents           : " << st.num_docs << "\n";
    std::cout << "  unique terms        : " << st.num_terms << "\n";
    std::cout << "  postings            : " << st.num_postings << "\n";
    std::cout << "  postings raw        : " << mib(st.raw_postings_bytes)
              << " MB  (" << st.raw_postings_bytes << " bytes, 8 B/posting)\n";
    std::cout << "  postings compressed : " << mib(st.compressed_postings_bytes)
              << " MB  (" << st.compressed_postings_bytes << " bytes)\n";
    std::cout << "  compression ratio   : " << ratio << "x  (compressed = "
              << pct << "% of raw)\n";
    std::cout << "  total index file    : " << mib(st.total_file_bytes)
              << " MB  (" << st.total_file_bytes << " bytes)\n";
}

// Single-index path (the original P2 behavior).
int build_single(const Args& args) {
    joho::InvertedIndex index;
    if (joho::load_corpus_tsv(args.corpus, index) == 0) return 1;
    try {
        print_stats(args.output, joho::write_index(index, args.output));
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << "\n";
        return 1;
    }
    return 0;
}

// Sharded path (P7): route each document to shard hash(id) % N, build N indexes,
// write <prefix>.<k>.joho. Hashing the id gives a roughly uniform split that's
// stable across runs (so the same doc always lands in the same shard).
int build_sharded(const Args& args) {
    std::ifstream in(args.corpus);
    if (!in) { std::cerr << "error: cannot open corpus '" << args.corpus << "'\n"; return 1; }

    const int N = args.shards;
    std::vector<joho::InvertedIndex> idx(N);
    std::hash<std::string> hasher;
    std::string line, id, text;
    std::size_t n_docs = 0;
    while (std::getline(in, line)) {
        if (!joho::split_first_tab(line, id, text)) continue;
        idx[hasher(id) % static_cast<std::size_t>(N)].add_document(id, text);
        if (++n_docs % 100000 == 0) std::cerr << "  ... routed " << n_docs << " docs\r";
    }
    std::cerr << "Routed " << n_docs << " docs across " << N << " shards\n";

    joho::IndexStats total;
    for (int k = 0; k < N; ++k) {
        idx[k].finalize();
        const std::string path = args.output + "." + std::to_string(k) + ".joho";
        try {
            const joho::IndexStats st = joho::write_index(idx[k], path);
            print_stats(path, st);
            total.num_docs += st.num_docs;
            total.num_terms += st.num_terms;  // summed (terms may repeat across shards)
            total.num_postings += st.num_postings;
            total.raw_postings_bytes += st.raw_postings_bytes;
            total.compressed_postings_bytes += st.compressed_postings_bytes;
            total.total_file_bytes += st.total_file_bytes;
        } catch (const std::exception& e) {
            std::cerr << "error: " << e.what() << "\n";
            return 1;
        }
    }
    std::cout << "\n=== sharded total (" << N << " shards) ===\n";
    print_stats(args.output + ".{0.." + std::to_string(N - 1) + "}.joho", total);
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    Args args;
    if (!parse(argc, argv, args)) return 1;
    return args.shards > 1 ? build_sharded(args) : build_single(args);
}
