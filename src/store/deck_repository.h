// Local deck persistence (M3.1/M3.2): one JSON file per deck under
// <base>/decks/, written atomically via temp-file + rename.
//
// Ports the webapp's Mongo-backed decks router to a per-install file store.
// The owner-header scoping collapses into a single shared store because every
// player runs the same local binary; the `owner` field is dropped from the
// document entirely.
#pragma once

#include "core/card.h"

#include <filesystem>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace mtgcpp::core {

// Why a deck could not be read (M3.2). `NotFound` also covers ids that are
// structurally invalid (they can never name a file), mirroring the webapp's
// 404 for a bad ObjectId.
enum class DeckReadError { NotFound, InvalidDeck };

class DeckRepository {
public:
  explicit DeckRepository(std::filesystem::path baseDir);

  // Persist a new deck. Assigns a fresh id (ignoring any provided one) and ISO
  // timestamps when absent, writes atomically, and returns the stored deck.
  // Throws std::runtime_error when the file cannot be written.
  Deck create(const Deck &deck);

  // The full deck for `id`, or the reason it could not be read. A corrupt file
  // surfaces as InvalidDeck rather than crashing, and leaves the store usable.
  std::variant<Deck, DeckReadError> read(const std::string &id) const;

  // All stored decks, newest created first. Unreadable files are skipped.
  std::vector<Deck> list() const;

  // Newest-first summary views for the deck list (Home screen); corrupt files
  // are skipped, and every summary is derived fresh from its cards.
  std::vector<DeckSummary> listSummaries() const;

  // Replace the deck with the matching id, preserving its created_at and
  // bumping updated_at. Returns false for an invalid id, a missing deck, or a
  // corrupt deck file.
  bool update(const Deck &deck);

  // Delete the deck file. Returns false for an invalid id or a missing deck
  // (a corrupt file is still removable — it exists on disk).
  bool remove(const std::string &id);

  const std::filesystem::path &dir() const { return baseDir_; }

private:
  std::filesystem::path filePathFor(const std::string &id) const;

  std::filesystem::path baseDir_;
};

// Derive the summary fields (total cards, unique printings, preview image)
// from a deck's cards, so listings never trust stale stored totals. Preview
// prefers the first card that has art, in webapp order: png, normal, large,
// small. Unique printings dedupe by lowercased (set, collector_number);
// printing-less cards dedupe by lowercased name.
DeckSummary deriveSummary(const Deck &deck);

} // namespace mtgcpp::core
