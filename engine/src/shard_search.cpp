// Joho engine — sharded scatter-gather retrieval driver (P7).
//
// Like joho_batch, but instead of one index it queries N on-disk shards in parallel
// and merges their results (see joho/sharded_searcher.hpp). Emits the same TREC run
// format, so the Python eval harness grades a sharded run exactly like a single one.
//
//   ./joho_shard_search --shards idx.0.joho idx.1.joho idx.2.joho idx.3.joho \
//                       --queries queries.tsv --output run_sharded.txt --top-k 1000
//
// The point of the experiment: compare this run against the single-index run to see
// how much (if anything) local-statistics scoring costs in ranking quality — the
// real distributed-IR trade-off, measured rather than assumed.

#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "joho/corpus.hpp"
#include "joho/sharded_searcher.hpp"

namespace {

struct Options {
    std::vector<std::string> shards;
    std::string queries_path;
    std::string output_path;
    std::size_t top_k = 1000;
    std::size_t per_shard_k = 0;  // 0 => default to top_k inside the searcher
    double k1 = 0.9;
    double b = 0.4;
    std::string tag = "joho-sharded";
};

void usage(const char* a0) {
    std::cerr
        << "Usage: " << a0 << " --shards F1 F2 ... --queries FILE [options]\n"
        << "  --shards F...     on-disk shard files (from joho_build --shards) (required, 1+)\n"
        << "  --queries FILE    TSV <qid>\\t<text>                              (required)\n"
        << "  --output FILE     write TREC run here              (default: stdout)\n"
        << "  --top-k N         global results per query         (default: 1000)\n"
        << "  --per-shard-k N   leaf results before the merge    (default: = top-k)\n"
        << "  --k1 F / --b F    BM25 params                      (default: 0.9 / 0.4)\n"
        << "  --tag S           run tag                          (default: joho-sharded)\n";
}

bool parse(int argc, char** argv, Options& o) {
    for (int i = 1; i < argc; ++i) {
        const std::string s = argv[i];
        auto next = [&](const char* n) -> std::string {
            if (i + 1 >= argc) { std::cerr << "error: " << n << " needs a value\n"; return ""; }
            return argv[++i];
        };
        if (s == "--shards") {
            // consume every following token until the next --flag
            while (i + 1 < argc && std::string(argv[i + 1]).rfind("--", 0) != 0) {
                o.shards.emplace_back(argv[++i]);
            }
        }
        else if (s == "--queries") o.queries_path = next("--queries");
        else if (s == "--output") o.output_path = next("--output");
        else if (s == "--top-k") o.top_k = std::stoul(next("--top-k"));
        else if (s == "--per-shard-k") o.per_shard_k = std::stoul(next("--per-shard-k"));
        else if (s == "--k1") o.k1 = std::stod(next("--k1"));
        else if (s == "--b") o.b = std::stod(next("--b"));
        else if (s == "--tag") o.tag = next("--tag");
        else if (s == "-h" || s == "--help") { usage(argv[0]); std::exit(0); }
        else { std::cerr << "error: unknown argument '" << s << "'\n"; return false; }
    }
    if (o.shards.empty() || o.queries_path.empty()) {
        std::cerr << "error: --shards (1+) and --queries are required\n\n";
        usage(argv[0]);
        return false;
    }
    return true;
}

}  // namespace

int main(int argc, char** argv) {
    Options opt;
    if (!parse(argc, argv, opt)) return 1;

    std::unique_ptr<joho::ShardedSearcher> searcher;
    try {
        searcher = std::make_unique<joho::ShardedSearcher>(opt.shards, opt.k1, opt.b);
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << "\n";
        return 1;
    }
    std::cerr << "Loaded " << searcher->num_shards() << " shards ("
              << searcher->total_docs() << " docs total)\n";

    std::ifstream queries(opt.queries_path);
    if (!queries) { std::cerr << "error: cannot open queries '" << opt.queries_path << "'\n"; return 1; }
    std::ofstream out_file;
    if (!opt.output_path.empty()) {
        out_file.open(opt.output_path);
        if (!out_file) { std::cerr << "error: cannot write '" << opt.output_path << "'\n"; return 1; }
    }
    std::ostream& run = opt.output_path.empty() ? std::cout : out_file;

    const auto start = std::chrono::steady_clock::now();
    std::size_t n_queries = 0, n_lines = 0;
    std::string line, qid, qtext;
    while (std::getline(queries, line)) {
        if (!joho::split_first_tab(line, qid, qtext)) continue;
        const auto hits = searcher->search(qtext, opt.top_k, opt.per_shard_k);
        int rank = 1;
        for (const joho::ScoredDoc& h : hits) {
            run << qid << " Q0 " << h.external_id << ' ' << rank++ << ' '
                << h.score << ' ' << opt.tag << '\n';
            ++n_lines;
        }
        ++n_queries;
    }
    const double elapsed = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - start).count();
    std::cerr << "Ran " << n_queries << " queries in " << elapsed << "s ("
              << (n_queries ? elapsed * 1000.0 / static_cast<double>(n_queries) : 0.0)
              << " ms/query); wrote " << n_lines << " run lines";
    if (!opt.output_path.empty()) std::cerr << " to " << opt.output_path;
    std::cerr << "\n";
    return 0;
}
