// ImportPreview operations implementation (M5.2): the preview state machine.
//
// These mirror the webapp reducer's preview handlers one-to-one: selection,
// replacement (keeping quantity + section), removal, dismissal and the guarded
// confirm. Identity is `missingCardKey` (`section|set|number`), the same key
// the webapp uses, so a replace/remove from the UI always finds the right row.

#include "core/import_preview.h"

#include "core/arena.h"

#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace mtgcpp::core {

ImportPreview makeImportPreview(const DeckParseResult &result) {
  return {result.cards, result.not_found, std::nullopt};
}

ImportPreview selectMissing(ImportPreview preview, const MissingCard &missing) {
  preview.activeMissing = missing;
  return preview;
}

ImportPreview replaceMissing(ImportPreview preview, const std::string &key, const Card &card) {
  // Find the flagged entry by identity; an unknown key is a no-op.
  const MissingCard *target = nullptr;
  for (const MissingCard &candidate : preview.missing) {
    if (missingCardKey(candidate) == key) {
      target = &candidate;
      break;
    }
  }
  if (target == nullptr) {
    return preview;
  }

  // The replacement inherits the flagged entry's quantity and section.
  Card replaced = card;
  replaced.quantity = target->quantity;
  replaced.section = target->section;
  preview.cards.push_back(std::move(replaced));

  std::vector<MissingCard> kept;
  kept.reserve(preview.missing.size() - 1);
  for (const MissingCard &candidate : preview.missing) {
    if (missingCardKey(candidate) != key) {
      kept.push_back(candidate);
    }
  }
  preview.missing = std::move(kept);
  preview.activeMissing.reset();
  return preview;
}

ImportPreview removeMissing(ImportPreview preview, const std::string &key) {
  std::vector<MissingCard> kept;
  kept.reserve(preview.missing.size());
  for (const MissingCard &candidate : preview.missing) {
    if (missingCardKey(candidate) != key) {
      kept.push_back(candidate);
    }
  }
  preview.missing = std::move(kept);
  preview.activeMissing.reset();
  return preview;
}

ImportPreview dismissMissing(ImportPreview preview) {
  preview.activeMissing.reset();
  return preview;
}

bool canConfirmImport(const ImportPreview &preview) { return preview.missing.empty(); }

std::optional<std::vector<Card>> confirmImport(const ImportPreview &preview) {
  if (!canConfirmImport(preview)) {
    return std::nullopt; // flagged lines must be resolved or removed first
  }
  return preview.cards;
}

} // namespace mtgcpp::core
