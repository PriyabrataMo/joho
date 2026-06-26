#pragma once
#include <cstddef>
#include <string>

#include "joho/inverted_index.hpp"

namespace joho {

// Split a TSV line on the FIRST tab into (id, text). The text field may contain
// spaces (we ask the Python exporter to strip tabs/newlines first), so one split
// is safe. Trims a trailing '\r' so CRLF files work. Returns false for blank
// lines or lines with no tab / empty id.
bool split_first_tab(const std::string& line, std::string& id, std::string& text);

// Stream a TSV corpus file (<doc_id>\t<text> per line) into `index` and call
// finalize(). Prints a heartbeat + summary to stderr. Returns the number of
// documents indexed, or 0 if the file could not be opened.
std::size_t load_corpus_tsv(const std::string& path, InvertedIndex& index);

}  // namespace joho
