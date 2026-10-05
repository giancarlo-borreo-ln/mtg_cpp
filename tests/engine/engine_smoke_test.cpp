// Engine linkage smoke test (Phase 1): the public `mtg_cpp` umbrella header,
// driven against ONLY `mtg_cpp_engine` (no SFML, no UI). Proves the engine is a
// self-contained reusable shared library: value types, the deck/profile stores,
// the board reducer, and the network envelope all link and work without any
// graphics dependency.

#include "mtg_cpp/mtg_cpp.h"

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <variant>

namespace {

using mtgcpp::core::Card;
using mtgcpp::core::DataPaths;
using mtgcpp::core::Deck;
using mtgcpp::core::DeckRepository;
using mtgcpp::net::Client;
using mtgcpp::state::applyAction;
using mtgcpp::state::initialBoardState;
using mtgcpp::state::tapCard;

// A throwaway directory removed on destruction, so tests never touch real data.
class TempDir {
public:
  TempDir() {
    const std::string unique =
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    path_ = std::filesystem::temp_directory_path() / ("mtgcpp_engine_" + unique);
    std::filesystem::create_directories(path_);
  }
  ~TempDir() { std::filesystem::remove_all(path_); }

  TempDir(const TempDir &) = delete;
  TempDir &operator=(const TempDir &) = delete;

  const std::filesystem::path &path() const { return path_; }

private:
  std::filesystem::path path_;
};

TEST(EngineSmoke, DataPathsDeriveFromRoot) {
  const DataPaths paths = DataPaths::fromRoot("/root");
  EXPECT_EQ(paths.cardDbCache(), std::filesystem::path("/root/card_db.cache"));
  EXPECT_EQ(paths.decksDir(), std::filesystem::path("/root/decks"));
  EXPECT_EQ(paths.profileFile(), std::filesystem::path("/root/profile.json"));
}

TEST(EngineSmoke, DeckRepositoryRoundTrip) {
  TempDir dir;
  DeckRepository repo(dir.path());
  Deck deck;
  deck.name = "Test Deck";
  deck.format = "Standard";
  Card card;
  card.name = "Lightning Bolt";
  card.scryfall_id = "abc";
  deck.cards.push_back(card);

  const Deck stored = repo.create(deck);
  EXPECT_FALSE(stored.id.empty());
  EXPECT_EQ(stored.name, "Test Deck");

  const std::variant<Deck, mtgcpp::core::DeckReadError> read = repo.read(stored.id);
  ASSERT_TRUE(std::holds_alternative<Deck>(read));
  EXPECT_EQ(std::get<Deck>(read), stored);
}

TEST(EngineSmoke, BoardReducerNoOpForUnknownCard) {
  const auto initial = initialBoardState();
  const auto bogus = tapCard(mtgcpp::core::PlayerSeat::Host, "no-such-card");
  EXPECT_EQ(applyAction(initial, bogus), initial);
}

TEST(EngineSmoke, ClientIsSafeBeforeAndAfterDisconnect) {
  Client client; // default-constructed: no socket, no io thread, nothing wired
  EXPECT_FALSE(client.connected());
  EXPECT_FALSE(client.receive(std::chrono::milliseconds{0}).has_value());
  client.disconnect(); // idempotent, must not touch an unstarted socket
  EXPECT_FALSE(client.connected());
}

} // namespace
