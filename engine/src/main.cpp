// Joho engine — P1 toy demo.
//
// Builds an inverted index over a tiny built-in corpus (no downloads needed),
// then runs BM25 so you can SEE words being scored and documents ranked.
//
//   ./joho                  -> runs a few illustrative demo queries
//   ./joho big cats hunt    -> searches the toy corpus for whatever you type

#include <iomanip>
#include <iostream>
#include <string>
#include <unordered_map>
#include <vector>

#include "joho/bm25.hpp"
#include "joho/inverted_index.hpp"

namespace {

struct Doc {
    std::string id;
    std::string text;
};

// 10 short documents about animals. Notice none of them contain the word "big"
// (they say "large"/"largest") — a deliberate reminder that BM25 only matches
// EXACT words. That gap is exactly what the dense/embedding layer fixes later.
std::vector<Doc> toy_corpus() {
    return {
        {"d1", "The lion is a large cat that lives in Africa. Lions hunt in groups called prides."},
        {"d2", "Tigers are the largest cats in the world. A tiger hunts alone in the forests of Asia."},
        {"d3", "House cats are small domestic animals kept as pets. A cat sleeps most of the day."},
        {"d4", "The African elephant is the largest land animal. Elephants live in herds."},
        {"d5", "Penguins are birds that cannot fly. They live in cold regions and hunt fish in the sea."},
        {"d6", "The cheetah is the fastest land animal and can run quickly to catch its prey."},
        {"d7", "Wolves hunt in packs. A wolf is a wild relative of the domestic dog."},
        {"d8", "Dogs are loyal domestic animals and popular pets. A dog can be trained easily."},
        {"d9", "Sharks are fish that live in the ocean. A shark has many rows of sharp teeth."},
        {"d10", "Eagles are large birds of prey with excellent eyesight that hunt small animals."},
    };
}

void run_query(const joho::BM25& bm25,
               const std::unordered_map<std::string, std::string>& text_by_id,
               const std::string& query) {
    std::cout << "\nQuery: \"" << query << "\"\n";
    std::cout << "  ----------------------------------------------------------------\n";
    const std::vector<joho::ScoredDoc> results = bm25.search(query, /*top_k=*/3);
    if (results.empty()) {
        std::cout << "  (no document contains any of those words)\n";
        return;
    }
    int rank = 1;
    for (const joho::ScoredDoc& r : results) {
        std::cout << "  #" << rank++ << "  score=" << std::fixed << std::setprecision(3)
                  << r.score << "  [" << r.external_id << "]  "
                  << text_by_id.at(r.external_id) << "\n";
    }
}

}  // namespace

int main(int argc, char** argv) {
    const std::vector<Doc> docs = toy_corpus();

    // 1. Build the inverted index.
    joho::InvertedIndex index;
    std::unordered_map<std::string, std::string> text_by_id;
    for (const Doc& d : docs) {
        index.add_document(d.id, d.text);
        text_by_id[d.id] = d.text;
    }
    index.finalize();

    std::cout << "Built inverted index: " << index.num_docs() << " documents, "
              << "average length " << std::fixed << std::setprecision(1) << index.avgdl()
              << " tokens.\n";

    // A couple of peeks into the index, so you can see what it stores.
    std::cout << "Posting list for \"hunt\": " << index.df("hunt") << " documents contain it.\n";
    std::cout << "Posting list for \"cat\":  " << index.df("cat") << " documents contain it.\n";

    // 2. Search.
    joho::BM25 bm25(index);

    if (argc > 1) {
        std::string query;
        for (int i = 1; i < argc; ++i) {
            if (i > 1) query += ' ';
            query += argv[i];
        }
        run_query(bm25, text_by_id, query);
    } else {
        run_query(bm25, text_by_id, "big cats that hunt");
        run_query(bm25, text_by_id, "domestic pets");
        run_query(bm25, text_by_id, "birds that hunt fish");
        std::cout << "\nTip: try your own query ->  ./joho wild animals that hunt in groups\n";
    }
    return 0;
}
