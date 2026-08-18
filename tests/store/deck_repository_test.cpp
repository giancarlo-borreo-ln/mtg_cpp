// M3.1/M3.2 deck repository tests: CRUD round-trip, atomic writes, id
// validation, summary derivation, and corruption handling on a temp dir. Ports
// backend/tests/test_decks_crud.py to the local file store (no HTTP, no Mongo).

#include "store/deck_repository.h"

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include <nlohmann/json.hpp>

namespace mtgcpp::core {
namespace {

// A card exercising every serialized field.
Card richCard() {
  Card card;
  card.scryfall_id = "id-1";
  card.name = "Akoum Warrior // Akoum Teeth";
  card.set_code = "znr";
  card.set_name = "Zendikar Rising";
  card.collector_number = "51a";
  card.quantity = 2;
  card.section = ArenaSection::Sideboard;
  card.mana_cost = "{3}{R}";
  card.cmc = 4.5f;
  card.colors = {"R"};
  card.type_line = "Creature — Human Warrior // Land";
  card.image_uris["png"] = "https://img/a.png";
  ScryfallFace face;
  face.name = "Akoum Warrior";
  face.image_uris["png"] = "https://img/front.png";
  card.card_faces.push_back(std::move(face));
  return card;
}

// A standard saved deck (id/timestamps empty; create() assigns them).
Deck makeDeck(std::string name) {
  Deck deck;
  deck.name = std::move(name);
  deck.format = "Standard";
  deck.total_cards = 2;
  deck.unique_cards = 1;
  deck.preview_image = "https://img/a.png";
  deck.cards = {richCard()};
  return deck;
}

std::vector<std::string> namesOf(const std::vector<Deck> &decks) {
  std::vector<std::string> names;
  names.reserve(decks.size());
  for (const Deck &deck : decks) {
    names.push_back(deck.name);
  }
  return names;
}

// A throwaway directory removed on destruction, so tests never touch real data.
class TempDir {
public:
  TempDir() {
    const std::string unique =
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    path_ = std::filesystem::temp_directory_path() / ("mtgcpp_repo_" + unique);
    std::filesystem::create_directories(path_);
  }
  ~TempDir() { std::filesystem::remove_all(path_); }

  TempDir(const TempDir &) = delete;
  TempDir &operator=(const TempDir &) = delete;
  TempDir(TempDir &&) = delete;
  TempDir &operator=(TempDir &&) = delete;

  const std::filesystem::path &path() const { return path_; }

private:
  std::filesystem::path path_;
};

// Read a deck and fail the test on a miss, reporting the error kind.
Deck mustRead(const DeckRepository &repo, const std::string &id) {
  const std::variant<Deck, DeckReadError> result = repo.read(id);
  if (std::holds_alternative<Deck>(result)) {
    return std::get<Deck>(result);
  }
  ADD_FAILURE() << "expected deck " << id << " to be readable, got error "
                << static_cast<int>(std::get<DeckReadError>(result));
  return Deck{};
}

// Assert a read fails with exactly `expected`.
void expectReadError(const DeckRepository &repo, const std::string &id, DeckReadError expected) {
  const std::variant<Deck, DeckReadError> result = repo.read(id);
  ASSERT_TRUE(std::holds_alternative<DeckReadError>(result));
  EXPECT_EQ(std::get<DeckReadError>(result), expected);
}

TEST(DeckRepository, CreateAssignsIdAndTimestampsAndWritesAFile) {
  TempDir dir;
  DeckRepository repo(dir.path());

  const Deck created = repo.create(makeDeck("Ghalta"));

  EXPECT_FALSE(created.id.empty());
  EXPECT_FALSE(created.created_at.empty());
  EXPECT_FALSE(created.updated_at.empty());
  EXPECT_TRUE(std::filesystem::exists(dir.path() / "decks" / (created.id + ".json")));
  // Atomic write must not leave a temporary sibling behind.
  EXPECT_FALSE(std::filesystem::exists(dir.path() / "decks" / (created.id + ".json.tmp")));
  EXPECT_EQ(mustRead(repo, created.id), created);
}

TEST(DeckRepository, ReadReportsNotFoundForAMissingDeck) {
  TempDir dir;
  DeckRepository repo(dir.path());

  expectReadError(repo, "0000000000000000", DeckReadError::NotFound);
}

TEST(DeckRepository, ReadReturnsNotFoundForInvalidIds) {
  TempDir dir;
  DeckRepository repo(dir.path());

  expectReadError(repo, "", DeckReadError::NotFound);
  expectReadError(repo, "../escape", DeckReadError::NotFound);
  expectReadError(repo, "a/b", DeckReadError::NotFound);
  expectReadError(repo, "..", DeckReadError::NotFound);
}

TEST(DeckRepository, ReadReportsInvalidDeckForACorruptFile) {
  TempDir dir;
  DeckRepository repo(dir.path());

  {
    std::ofstream garbage(dir.path() / "decks" / "aaaaaaaaaaaaaaaa.json");
    garbage << "this is not json";
  }
  {
    std::ofstream emptyObject(dir.path() / "decks" / "bbbbbbbbbbbbbbbb.json");
    emptyObject << "{}";
  }

  expectReadError(repo, "aaaaaaaaaaaaaaaa", DeckReadError::InvalidDeck);
  expectReadError(repo, "bbbbbbbbbbbbbbbb", DeckReadError::InvalidDeck);
}

TEST(DeckRepository, ListReturnsCreatedDecks) {
  TempDir dir;
  DeckRepository repo(dir.path());

  repo.create(makeDeck("One"));
  repo.create(makeDeck("Two"));

  const std::vector<Deck> decks = repo.list();
  ASSERT_EQ(decks.size(), 2u);
  // Named locals: two calls to namesOf(decks) would produce iterators into two
  // different temporaries (the set range constructor expects one container).
  const std::vector<std::string> names = namesOf(decks);
  EXPECT_EQ(std::set<std::string>(names.begin(), names.end()),
            (std::set<std::string>{"One", "Two"}));
}

TEST(DeckRepository, ListReturnsEmptyWithoutDecks) {
  TempDir dir;
  DeckRepository repo(dir.path());

  EXPECT_TRUE(repo.list().empty());
}

TEST(DeckRepository, UpdateReplacesFieldsAndPreservesCreatedAt) {
  TempDir dir;
  DeckRepository repo(dir.path());
  const Deck created = repo.create(makeDeck("Original"));

  Deck updated = created;
  updated.name = "Renamed";
  updated.format = "Commander";
  updated.total_cards = 4;
  updated.cards.clear();
  EXPECT_TRUE(repo.update(updated));

  const Deck stored = mustRead(repo, created.id);
  EXPECT_EQ(stored.name, "Renamed");
  EXPECT_EQ(stored.format, "Commander");
  EXPECT_EQ(stored.total_cards, 4);
  EXPECT_TRUE(stored.cards.empty());
  EXPECT_EQ(stored.created_at, created.created_at);
  EXPECT_TRUE(stored.updated_at >= created.updated_at);
}

TEST(DeckRepository, UpdateReturnsFalseForAMissingDeck) {
  TempDir dir;
  DeckRepository repo(dir.path());

  Deck deck = makeDeck("X");
  deck.id = "0000000000000000";
  EXPECT_FALSE(repo.update(deck));
}

TEST(DeckRepository, UpdateReturnsFalseForAnInvalidId) {
  TempDir dir;
  DeckRepository repo(dir.path());

  Deck deck = makeDeck("X");
  deck.id = "../escape";
  EXPECT_FALSE(repo.update(deck));
}

TEST(DeckRepository, UpdateFailsForACorruptDeck) {
  TempDir dir;
  DeckRepository repo(dir.path());
  const std::string corruptId = "cccccccccccccccc";
  {
    std::ofstream out(dir.path() / "decks" / (corruptId + ".json"));
    out << "garbage";
  }

  Deck deck = makeDeck("X");
  deck.id = corruptId;
  EXPECT_FALSE(repo.update(deck));
}

TEST(DeckRepository, RemoveDeletesTheDeck) {
  TempDir dir;
  DeckRepository repo(dir.path());
  const Deck created = repo.create(makeDeck("Temporary"));

  EXPECT_TRUE(repo.remove(created.id));
  expectReadError(repo, created.id, DeckReadError::NotFound);
  EXPECT_TRUE(repo.list().empty());
}

TEST(DeckRepository, RemoveReturnsFalseForAMissingDeck) {
  TempDir dir;
  DeckRepository repo(dir.path());

  EXPECT_FALSE(repo.remove("0000000000000000"));
}

TEST(DeckRepository, RemoveReturnsFalseForAnInvalidId) {
  TempDir dir;
  DeckRepository repo(dir.path());

  EXPECT_FALSE(repo.remove(""));
  EXPECT_FALSE(repo.remove("../escape"));
}

TEST(DeckRepository, RemoveDeletesACorruptDeck) {
  TempDir dir;
  DeckRepository repo(dir.path());
  const std::string corruptId = "cccccccccccccccc";
  {
    std::ofstream out(dir.path() / "decks" / (corruptId + ".json"));
    out << "garbage";
  }

  EXPECT_TRUE(repo.remove(corruptId));
  expectReadError(repo, corruptId, DeckReadError::NotFound);
}

TEST(DeckRepository, RoundTripPreservesEveryCardField) {
  TempDir dir;
  DeckRepository repo(dir.path());

  Deck deck;
  deck.name = "Round Trip";
  deck.format = "Commander";
  deck.total_cards = 3;
  deck.unique_cards = 2;
  deck.preview_image = std::nullopt;
  deck.cards = {richCard(), Card{}};

  const Deck created = repo.create(deck);
  EXPECT_EQ(mustRead(repo, created.id), created);
}

TEST(DeckRepository, CorruptDeckLeavesTheStoreUsable) {
  TempDir dir;
  DeckRepository repo(dir.path());

  const std::string corruptId = "cccccccccccccccc";
  {
    std::ofstream out(dir.path() / "decks" / (corruptId + ".json"));
    out << "garbage";
  }
  const Deck good = repo.create(makeDeck("Good"));

  expectReadError(repo, corruptId, DeckReadError::InvalidDeck);
  EXPECT_EQ(mustRead(repo, good.id).name, "Good");

  const std::vector<Deck> decks = repo.list();
  ASSERT_EQ(decks.size(), 1u);
  EXPECT_EQ(decks.at(0).id, good.id);
  EXPECT_EQ(repo.listSummaries().size(), 1u);

  Deck renamed = good;
  renamed.name = "Renamed";
  EXPECT_TRUE(repo.update(renamed));
  EXPECT_EQ(mustRead(repo, good.id).name, "Renamed");

  EXPECT_TRUE(repo.remove(corruptId));
  expectReadError(repo, corruptId, DeckReadError::NotFound);
}

TEST(DeckRepository, ListSkipsTemporaryAndNonJsonFiles) {
  TempDir dir;
  DeckRepository repo(dir.path());
  const Deck created = repo.create(makeDeck("Real"));

  {
    std::ofstream leftover(dir.path() / "decks" / "aaaaaaaaaaaaaaaa.json.tmp");
    leftover << "{}";
    std::ofstream other(dir.path() / "decks" / "notes.txt");
    other << "not a deck";
  }

  const std::vector<Deck> decks = repo.list();
  ASSERT_EQ(decks.size(), 1u);
  EXPECT_EQ(decks.at(0).name, "Real");
  EXPECT_EQ(decks.at(0).id, created.id);
}

TEST(DeckRepository, ListSummariesDerivesTotalUniqueAndPreview) {
  TempDir dir;
  DeckRepository repo(dir.path());

  Deck deck;
  deck.name = "Forests";
  deck.format = "Standard";
  deck.cards = {richCard(), richCard()};

  repo.create(deck);

  const std::vector<DeckSummary> summaries = repo.listSummaries();
  ASSERT_EQ(summaries.size(), 1u);
  const DeckSummary &summary = summaries.at(0);
  EXPECT_EQ(summary.name, "Forests");
  EXPECT_EQ(summary.format, "Standard");
  EXPECT_EQ(summary.total_cards, 4);
  EXPECT_EQ(summary.unique_cards, 1);
  EXPECT_EQ(summary.preview_image.value_or(""), "https://img/a.png");
  EXPECT_FALSE(summary.created_at.empty());
  EXPECT_FALSE(summary.updated_at.empty());
}

TEST(DeriveSummary, PrefersPngOverNormalSize) {
  Card card;
  card.name = "Card";
  card.image_uris["small"] = "https://img/s.jpg";
  card.image_uris["normal"] = "https://img/n.jpg";
  Deck deck;
  deck.cards = {card};

  EXPECT_EQ(deriveSummary(deck).preview_image.value_or(""), "https://img/n.jpg");

  card.image_uris["png"] = "https://img/c.png";
  deck.cards = {card};
  EXPECT_EQ(deriveSummary(deck).preview_image.value_or(""), "https://img/c.png");
}

TEST(DeriveSummary, UsesTheFirstCardsImage) {
  Card noImage;
  noImage.name = "A";
  Card withImage;
  withImage.name = "B";
  withImage.image_uris["png"] = "https://img/b.png";
  Deck deck;
  deck.cards = {noImage, withImage};

  EXPECT_EQ(deriveSummary(deck).preview_image.value_or(""), "https://img/b.png");
}

TEST(DeriveSummary, ReturnsNulloptWithoutImages) {
  Card card;
  card.name = "Plain";
  Deck deck;
  deck.cards = {card};

  EXPECT_FALSE(deriveSummary(deck).preview_image.has_value());
}

TEST(DeriveSummary, CountsNameOnlyCardsDistinctly) {
  Card alpha;
  alpha.name = "Alpha";
  Card beta;
  beta.name = "Beta";
  Card alphaAgain;
  alphaAgain.name = "Alpha";
  Deck deck;
  deck.cards = {alpha, beta};

  EXPECT_EQ(deriveSummary(deck).unique_cards, 2);

  deck.cards = {alpha, alphaAgain};
  EXPECT_EQ(deriveSummary(deck).unique_cards, 1);
}

TEST(DeriveSummary, HandlesEmptyCards) {
  const DeckSummary summary = deriveSummary(Deck{});
  EXPECT_EQ(summary.total_cards, 0);
  EXPECT_EQ(summary.unique_cards, 0);
  EXPECT_FALSE(summary.preview_image.has_value());
}

TEST(DefaultDataDir, ReturnsANonEmptyPath) { EXPECT_FALSE(defaultDataDir().empty()); }

} // namespace
} // namespace mtgcpp::core
