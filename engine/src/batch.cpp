// Joho engine — batch retrieval driver (P1 keystone).
//
// This is the "production" entry point (as opposed to the toy demo in main.cpp).
// It does exactly what an offline evaluation needs:
//
//   1. read a CORPUS file  (TSV: <doc_id> \t <text>)        -> build the index
//   2. read a QUERIES file (TSV: <query_id> \t <query_text>) -> run BM25 on each
//   3. write a RUN file in TREC format                       -> graded by Python
//
// TREC run format (one line per (query, retrieved doc), the de-facto standard
// that ir_measures / trec_eval understand):
//
//     <query_id> Q0 <doc_id> <rank> <score> <run_tag>
//
//   - "Q0" is a historical placeholder column (always the literal Q0).
//   - rank is 1-based, in score-descending order.
//   - run_tag names the system, so multiple runs can be compared in one table.
//
// Why a separate binary? The eval harness, the toy demo, and (later) the gRPC
// server all link the same joho_core library, but each has its own thin "main".
//
// Example:
//   ./joho_batch --corpus corpus.tsv --queries queries.tsv \
//                --output run.txt --top-k 1000 --k1 0.9 --b 0.4 --tag joho-bm25

#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "joho/bm25.hpp"
#include "joho/corpus.hpp"
#include "joho/disk_index.hpp"
#include "joho/index_reader.hpp"
#include "joho/inverted_index.hpp"

namespace {

// All the knobs this tool accepts, with sensible defaults.
struct Options {
    std::string corpus_path;        // build the index in RAM from this corpus...
    std::string index_path;         // ...or mmap a prebuilt on-disk index instead
    std::string queries_path;
    std::string output_path;        // empty => write the run to stdout
    std::size_t top_k = 1000;       // 1000 is the standard depth for IR eval
    double k1 = 0.9;                // matches BM25 defaults in bm25.hpp
    double b = 0.4;
    std::string tag = "joho-bm25";  // the run_tag column
};

void print_usage(const char* argv0) {
    std::cerr
        << "Usage: " << argv0 << " (--corpus FILE | --index FILE) --queries FILE [options]\n"
        << "\n"
        << "  --corpus   FILE   TSV of <doc_id>\\t<text>; built in RAM   (corpus OR index)\n"
        << "  --index    FILE   prebuilt on-disk index (mmapped)        (corpus OR index)\n"
        << "  --queries  FILE   TSV of <query_id>\\t<text>    (required)\n"
        << "  --output   FILE   write TREC run here           (default: stdout)\n"
        << "  --top-k    N      results per query             (default: 1000)\n"
        << "  --k1       F      BM25 k1                        (default: 0.9)\n"
        << "  --b        F      BM25 b                         (default: 0.4)\n"
        << "  --tag      S      run tag in the run file        (default: joho-bm25)\n";
}

// Tiny hand-rolled argument parser. Each flag below expects one value after it.
bool parse_args(int argc, char** argv, Options& opt) {
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        auto next = [&](const char* name) -> std::string {
            if (i + 1 >= argc) {
                std::cerr << "error: " << name << " needs a value\n";
                return std::string();
            }
            return argv[++i];
        };
        if (arg == "--corpus") opt.corpus_path = next("--corpus");
        else if (arg == "--index") opt.index_path = next("--index");
        else if (arg == "--queries") opt.queries_path = next("--queries");
        else if (arg == "--output") opt.output_path = next("--output");
        else if (arg == "--top-k") opt.top_k = std::stoul(next("--top-k"));
        else if (arg == "--k1") opt.k1 = std::stod(next("--k1"));
        else if (arg == "--b") opt.b = std::stod(next("--b"));
        else if (arg == "--tag") opt.tag = next("--tag");
        else if (arg == "-h" || arg == "--help") { print_usage(argv[0]); std::exit(0); }
        else { std::cerr << "error: unknown argument '" << arg << "'\n"; return false; }
    }
    if (opt.queries_path.empty() || (opt.corpus_path.empty() && opt.index_path.empty())) {
        std::cerr << "error: --queries and one of (--corpus | --index) are required\n\n";
        print_usage(argv[0]);
        return false;
    }
    if (!opt.corpus_path.empty() && !opt.index_path.empty()) {
        std::cerr << "error: use either --corpus or --index, not both\n\n";
        print_usage(argv[0]);
        return false;
    }
    return true;
}

double seconds_since(std::chrono::steady_clock::time_point start) {
    const auto now = std::chrono::steady_clock::now();
    return std::chrono::duration<double>(now - start).count();
}

}  // namespace

int main(int argc, char** argv) {
    Options opt;
    if (!parse_args(argc, argv, opt)) return 1;

    // 1. Get an IndexReader. BM25 doesn't care how it's backed: either build the
    //    inverted index in RAM from the corpus, or mmap a prebuilt on-disk index.
    joho::InvertedIndex mem_index;
    std::unique_ptr<joho::DiskIndex> disk_index;
    const joho::IndexReader* reader = nullptr;
    if (!opt.index_path.empty()) {
        try {
            disk_index = std::make_unique<joho::DiskIndex>(opt.index_path);
        } catch (const std::exception& e) {
            std::cerr << "error: " << e.what() << "\n";
            return 1;
        }
        reader = disk_index.get();
        std::cerr << "Loaded on-disk index '" << opt.index_path << "' ("
                  << reader->num_docs() << " docs, avgdl=" << reader->avgdl() << ")\n";
    } else {
        if (joho::load_corpus_tsv(opt.corpus_path, mem_index) == 0) return 1;
        reader = &mem_index;
    }

    // 2. Open the queries file and the run-file sink (file or stdout).
    std::ifstream queries(opt.queries_path);
    if (!queries) {
        std::cerr << "error: cannot open queries '" << opt.queries_path << "'\n";
        return 1;
    }
    std::ofstream out_file;
    if (!opt.output_path.empty()) {
        out_file.open(opt.output_path);
        if (!out_file) {
            std::cerr << "error: cannot write output '" << opt.output_path << "'\n";
            return 1;
        }
    }
    std::ostream& run = opt.output_path.empty() ? std::cout : out_file;

    // 3. Score every query and emit TREC run lines.
    joho::BM25 bm25(*reader, opt.k1, opt.b);
    const auto start = std::chrono::steady_clock::now();
    std::size_t n_queries = 0, n_lines = 0;
    std::string line, qid, qtext;
    while (std::getline(queries, line)) {
        if (!joho::split_first_tab(line, qid, qtext)) continue;
        const std::vector<joho::ScoredDoc> hits = bm25.search(qtext, opt.top_k);
        int rank = 1;
        for (const joho::ScoredDoc& h : hits) {
            run << qid << " Q0 " << h.external_id << ' ' << rank++ << ' '
                << h.score << ' ' << opt.tag << '\n';
            ++n_lines;
        }
        ++n_queries;
    }
    const double elapsed = seconds_since(start);
    std::cerr << "Ran " << n_queries << " queries in " << elapsed << "s ("
              << (n_queries ? elapsed * 1000.0 / static_cast<double>(n_queries) : 0.0)
              << " ms/query); wrote " << n_lines << " run lines";
    if (!opt.output_path.empty()) std::cerr << " to " << opt.output_path;
    std::cerr << "\n";
    return 0;
}
