// Fuzz target: the Arena deck parser (M11.1).
//
// Feeds arbitrary bytes through `parseArenaSections` + `aggregateEntries`,
// which is exactly the untrusted-input path the importer runs on Arena export
// text. The parser is deliberately resilient (malformed lines are skipped), and
// only a text with zero parseable lines raises DeckParseError — which is the
// documented, expected behavior, so the target catches it. Anything else that
// escapes (a crash, UB, an uncaught exception) aborts the fuzz run.

#include "core/deck_parser.h"
#include "fuzz/fuzz_target.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, std::size_t size) {
  std::string text(size, '\0');
  if (size != 0u) {
    std::memcpy(text.data(), data, size);
  }
  try {
    const mtgcpp::core::ParsedSections sections = mtgcpp::core::parseArenaSections(text);
    for (const auto &[section, entries] : sections) {
      (void)section;
      mtgcpp::core::aggregateEntries(entries);
    }
  } catch (const mtgcpp::core::DeckParseError &) {
    // Expected and documented: a text with zero parseable card lines. The
    // parser must never crash on untrusted input, so this is a no-op.
    (void)0;
  }
  return 0;
}

#ifdef MTG_CPP_FUZZ_DRIVER
int main(int argc, char **argv) {
  return mtgcpp::fuzz::runDriverMain(&LLVMFuzzerTestOneInput, argc, argv,
                                     "src/fuzz/seed_corpus/deck_parser");
}
#endif
