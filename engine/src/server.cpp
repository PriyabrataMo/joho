// Joho engine — gRPC server (P6).
//
// Exposes the C++ retrieval engine over the contract in proto/joho.proto so the
// Python gateway can orchestrate the funnel. This binary is built only when gRPC +
// Protobuf are installed (see engine/CMakeLists.txt); the rest of the engine builds
// without them.
//
// What it serves:
//   * Search       — BM25 over the on-disk index (or an in-RAM index built from the
//                    corpus), optionally returning each hit's document text so the
//                    gateway can re-rank without its own corpus copy.
//   * Autocomplete — weighted-trie prefix completions (P5).
//   * Correct      — SymSpell "did you mean?" (P5).
//   * Health       — doc count + readiness, for load-balancer / Cloud Run probes.
//
// Run:
//   ./joho_server --corpus corpus.tsv [--index index.joho] [--port 50051]
//
//   --corpus is required: it provides the document text (for Search include_text)
//   and the vocabulary for autocomplete/correct. --index (optional) memory-maps a
//   prebuilt on-disk index for BM25 instead of building one in RAM at startup.

#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <unordered_map>

#include <grpcpp/grpcpp.h>

#include "joho.grpc.pb.h"

#include "joho/bm25.hpp"
#include "joho/corpus.hpp"
#include "joho/disk_index.hpp"
#include "joho/inverted_index.hpp"
#include "joho/suggest_index.hpp"

namespace {

double now_ms(std::chrono::steady_clock::time_point t0) {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
}

// Loads the corpus text once into a doc_id -> text map (for Search include_text).
std::unordered_map<std::string, std::string> load_text_map(const std::string& corpus_path) {
    std::unordered_map<std::string, std::string> m;
    std::ifstream in(corpus_path);
    std::string line, id, text;
    while (std::getline(in, line)) {
        if (joho::split_first_tab(line, id, text)) m.emplace(std::move(id), std::move(text));
    }
    return m;
}

}  // namespace

// The service implementation. Holds the index (BM25 backend), the document text map,
// and the query-assist structures; all are read-only after startup, so the default
// gRPC thread pool can serve requests concurrently without locking.
class JohoServiceImpl final : public joho::Joho::Service {
public:
    JohoServiceImpl(const std::string& corpus_path, const std::string& index_path)
        : index_name_(index_path.empty() ? corpus_path : index_path) {
        // BM25 backend: mmap the on-disk index if given, else build one in RAM.
        if (!index_path.empty()) {
            disk_ = std::make_unique<joho::DiskIndex>(index_path);
            reader_ = disk_.get();
            std::cerr << "Loaded on-disk index '" << index_path << "' ("
                      << reader_->num_docs() << " docs)\n";
        } else {
            mem_ = std::make_unique<joho::InvertedIndex>();
            if (joho::load_corpus_tsv(corpus_path, *mem_) == 0) {
                throw std::runtime_error("failed to load corpus '" + corpus_path + "'");
            }
            reader_ = mem_.get();
        }
        bm25_ = std::make_unique<joho::BM25>(*reader_);

        // Document text (for include_text) + query-assist vocabulary.
        text_ = load_text_map(corpus_path);
        suggest_.build_from_corpus(corpus_path);
        std::cerr << "Server ready: " << reader_->num_docs() << " docs, "
                  << suggest_.vocab_size() << " vocab terms, "
                  << text_.size() << " texts cached\n";
    }

    grpc::Status Search(grpc::ServerContext*, const joho::SearchRequest* req,
                        joho::SearchResponse* resp) override {
        const auto t0 = std::chrono::steady_clock::now();
        const std::size_t k = req->top_k() ? req->top_k() : 1000;
        const auto hits = bm25_->search(req->query(), k);
        uint32_t rank = 1;
        for (const joho::ScoredDoc& h : hits) {
            joho::Hit* hit = resp->add_hits();
            hit->set_doc_id(h.external_id);
            hit->set_bm25_score(h.score);
            hit->set_rank(rank++);
            if (req->include_text()) {
                const auto it = text_.find(h.external_id);
                if (it != text_.end()) hit->set_text(it->second);
            }
        }
        resp->set_took_ms(now_ms(t0));
        return grpc::Status::OK;
    }

    grpc::Status Autocomplete(grpc::ServerContext*, const joho::AutocompleteRequest* req,
                              joho::AutocompleteResponse* resp) override {
        const std::size_t k = req->k() ? req->k() : 8;
        for (const joho::Completion& c : suggest_.complete(req->prefix(), k)) {
            joho::Completion* out = resp->add_completions();
            out->set_term(c.term);
            out->set_weight(c.weight);
        }
        return grpc::Status::OK;
    }

    grpc::Status Correct(grpc::ServerContext*, const joho::CorrectRequest* req,
                         joho::CorrectResponse* resp) override {
        const std::size_t k = req->k() ? req->k() : 5;
        const auto all = suggest_.correct_all(req->word(), k);
        if (all.empty()) { resp->set_found(false); return grpc::Status::OK; }
        const joho::Suggestion& best = all.front();
        resp->set_found(true);
        resp->set_term(best.term);
        resp->set_distance(best.distance);
        resp->set_frequency(best.frequency);
        for (std::size_t i = 1; i < all.size(); ++i) resp->add_alternatives(all[i].term);
        return grpc::Status::OK;
    }

    grpc::Status Health(grpc::ServerContext*, const joho::HealthRequest*,
                        joho::HealthResponse* resp) override {
        resp->set_ok(true);
        resp->set_num_docs(reader_->num_docs());
        resp->set_index_name(index_name_);
        resp->set_suggest_ready(suggest_.vocab_size() > 0);
        return grpc::Status::OK;
    }

private:
    std::string index_name_;
    std::unique_ptr<joho::DiskIndex> disk_;
    std::unique_ptr<joho::InvertedIndex> mem_;
    const joho::IndexReader* reader_ = nullptr;
    std::unique_ptr<joho::BM25> bm25_;
    std::unordered_map<std::string, std::string> text_;
    joho::SuggestIndex suggest_;
};

int main(int argc, char** argv) {
    std::string corpus, index, port = "50051";
    for (int i = 1; i < argc; ++i) {
        const std::string s = argv[i];
        auto next = [&]() -> std::string { return (i + 1 < argc) ? std::string(argv[++i]) : ""; };
        if (s == "--corpus") corpus = next();
        else if (s == "--index") index = next();
        else if (s == "--port") port = next();
        else if (s == "-h" || s == "--help") {
            std::cerr << "Usage: joho_server --corpus FILE [--index FILE] [--port 50051]\n";
            return 0;
        } else { std::cerr << "error: unknown argument '" << s << "'\n"; return 1; }
    }
    if (corpus.empty()) { std::cerr << "error: --corpus is required\n"; return 1; }

    std::unique_ptr<JohoServiceImpl> service;
    try {
        service = std::make_unique<JohoServiceImpl>(corpus, index);
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << "\n";
        return 1;
    }

    const std::string addr = "0.0.0.0:" + port;
    grpc::ServerBuilder builder;
    builder.AddListeningPort(addr, grpc::InsecureServerCredentials());
    builder.RegisterService(service.get());
    // Allow large responses (top-1000 hits with text can exceed the 4 MB default).
    builder.SetMaxSendMessageSize(64 * 1024 * 1024);
    std::unique_ptr<grpc::Server> server(builder.BuildAndStart());
    if (!server) { std::cerr << "error: failed to bind " << addr << "\n"; return 1; }
    std::cerr << "joho_server listening on " << addr << "\n";
    server->Wait();
    return 0;
}
