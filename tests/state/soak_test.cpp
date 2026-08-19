// M10.3 soak test: two loopback peers play a long, rapid sequence of board
// actions and must converge on byte-identical battlefields with no drift, no
// echo (mirrored `board_update`s are never re-sent), and no leaks (the whole
// suite runs under ASan/UBSan). A second test plays a synced match while the
// table's art cache is live, proving the procedural fallback is replaced by
// real art on both sides.

#include "net/client.h"
#include "net/envelope.h"
#include "net/server.h"
#include "net/transport.h"
#include "state/board_state.h"
#include "state/session.h"
#include "ui/art_cache.h"
#include "ui/screens/table_screen.h"

#include <gtest/gtest.h>

#include <SFML/Graphics.hpp>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstring>
#include <filesystem>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace mtgcpp::state {
namespace {

using mtgcpp::core::Deck;
using mtgcpp::core::PlayerSeat;
using mtgcpp::core::PlayerZone;

using namespace std::chrono_literals;

// A peer at the table: its own client + session.
struct Peer {
  mtgcpp::net::Client client;
  Session session;

  Peer() : session(client) {}
};

void pump(mtgcpp::net::Server &server, const std::vector<Peer *> &peers) {
  server.runOnce();
  for (Peer *peer : peers) {
    peer->session.drain();
  }
}

template <typename Predicate>
bool waitUntil(Predicate predicate, mtgcpp::net::Server &server, const std::vector<Peer *> &peers,
               std::chrono::milliseconds timeout = 5s) {
  const std::chrono::steady_clock::time_point deadline = std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < deadline) {
    pump(server, peers);
    if (predicate()) {
      return true;
    }
    std::this_thread::sleep_for(1ms);
  }
  pump(server, peers);
  return predicate();
}

mtgcpp::core::Card card(const std::string &name, std::string type_line, int quantity) {
  auto result = mtgcpp::core::Card{};
  result.name = name;
  result.set_code = "sta";
  result.set_name = "Test Set";
  result.collector_number = "1";
  result.quantity = quantity;
  result.type_line = std::move(type_line);
  result.scryfall_id = "scry-" + name;
  result.image_uris["png"] = "https://img.example.invalid/" + name + ".png";
  return result;
}

Deck deckA() {
  Deck deck;
  deck.id = "dA";
  deck.name = "Host Deck";
  deck.format = "Standard";
  deck.cards = {card("Bolt", "Instant", 4), card("Forest", "Basic Land — Forest", 4)};
  return deck;
}

Deck deckB() {
  Deck deck;
  deck.id = "dB";
  deck.name = "Guest Deck";
  deck.format = "Standard";
  deck.cards = {card("Blaze", "Instant", 4), card("Island", "Basic Land — Island", 4)};
  return deck;
}

const SeatBoard &seatOf(const Session &session, PlayerSeat seat) {
  return session.board().seats.at(seatIndex(seat));
}

bool sameBattlefield(const BoardState &a, const BoardState &b) {
  return a.seats == b.seats && a.stack == b.stack && a.life == b.life;
}

// A two-peer match on real loopback sockets, ready at the table.
struct Match {
  mtgcpp::net::AsioTransport transport{0};
  mtgcpp::net::Server server{transport};
  Peer host;
  Peer guest;
  std::vector<Peer *> peers{&host, &guest};

  Match() { server.start(); }

  ~Match() { server.stop(); }
  Match(const Match &) = delete;
  Match &operator=(const Match &) = delete;
  Match(Match &&) = delete;
  Match &operator=(Match &&) = delete;

  bool join() {
    const std::string port = std::to_string(transport.localPort());
    if (!host.client.connect("127.0.0.1", port) || !guest.client.connect("127.0.0.1", port)) {
      return false;
    }
    return waitUntil([&] { return host.session.isReady() && guest.session.isReady(); }, server,
                     peers);
  }

  bool chooseDecks() {
    host.session.chooseDeck(deckA());
    guest.session.chooseDeck(deckB());
    return waitUntil([&] { return host.session.canStartTable() && guest.session.canStartTable(); },
                     server, peers);
  }
};

TEST(Soak, RapidMovesConvergeWithNoDriftOrEcho) {
  Match match;
  ASSERT_TRUE(match.join());
  ASSERT_TRUE(match.chooseDecks());
  EXPECT_TRUE(sameBattlefield(match.host.session.board(), match.guest.session.board()));

  // Card ids for the whole soak: tapping/countering/flipping the same instance
  // is valid forever, and both peers mint identical seats.
  const SeatBoard &hostBoard = seatOf(match.host.session, PlayerSeat::Host);
  const std::string h1 = hostBoard.hand.at(0).id;
  const std::string h2 = hostBoard.hand.at(1).id;
  const std::string h3 = hostBoard.hand.at(2).id;
  const SeatBoard &guestBoard = seatOf(match.guest.session, PlayerSeat::Guest);
  const std::string g1 = guestBoard.hand.at(0).id;
  const std::string g2 = guestBoard.hand.at(1).id;

  constexpr int kIterations = 400;
  for (int i = 0; i < kIterations; ++i) {
    match.host.session.applyLocalAction(tapCard(PlayerSeat::Host, h1));
    match.host.session.applyLocalAction(addCounter(PlayerSeat::Host, h2));
    match.host.session.applyLocalAction(flipCard(PlayerSeat::Host, h3));
    match.host.session.applyLocalAction(setLife(PlayerSeat::Host, 20 - (i % 10)));
    match.guest.session.applyLocalAction(tapCard(PlayerSeat::Guest, g1));
    match.guest.session.applyLocalAction(addCounter(PlayerSeat::Guest, g2));
    match.guest.session.applyLocalAction(setLife(PlayerSeat::Guest, 20 - (i % 7)));
    pump(match.server, match.peers);
    // Every 50 iterations, wait for every frame so far to land, then assert
    // the boards never drifted (rapid fire must not drop or reorder anything).
    if (i % 50 == 0) {
      const std::size_t expectedHost = 3u * static_cast<std::size_t>(i + 1);
      const std::size_t expectedGuest = 4u * static_cast<std::size_t>(i + 1);
      const bool caughtUp = waitUntil(
          [&] {
            return match.host.session.boardUpdatesApplied() >= expectedHost &&
                   match.guest.session.boardUpdatesApplied() >= expectedGuest;
          },
          match.server, match.peers);
      ASSERT_TRUE(caughtUp) << "iteration " << i;
      ASSERT_TRUE(sameBattlefield(match.host.session.board(), match.guest.session.board()))
          << "iteration " << i;
    }
  }

  ASSERT_TRUE(waitUntil(
      [&] { return sameBattlefield(match.host.session.board(), match.guest.session.board()); },
      match.server, match.peers));

  const BoardState &board = match.host.session.board();
  EXPECT_EQ(board.life.at(seatIndex(PlayerSeat::Host)), 20 - ((kIterations - 1) % 10));
  EXPECT_EQ(board.life.at(seatIndex(PlayerSeat::Guest)), 20 - ((kIterations - 1) % 7));
  // The h2 instance accumulated exactly one counter per iteration.
  const SeatBoard &hostSeatFinal = seatOf(match.host.session, PlayerSeat::Host);
  bool found = false;
  for (const mtgcpp::core::BoardCard &candidate : hostSeatFinal.hand) {
    if (candidate.id == h2) {
      EXPECT_EQ(candidate.counters, kIterations);
      found = true;
    }
  }
  EXPECT_TRUE(found);

  // Echo suppression: each peer applies exactly as many inbound updates as the
  // opponent's local actions — a re-sent mirror would arrive back and inflate
  // the count. (The host applies 3/iteration, the guest 4/iteration.)
  EXPECT_EQ(match.host.session.boardUpdatesApplied(), 3u * kIterations);
  EXPECT_EQ(match.guest.session.boardUpdatesApplied(), 4u * kIterations);

  match.host.session.leave();
  match.guest.session.leave();
}

// True once BOTH tables resolved a real art texture for some face-up card (the
// procedural fallback is replaced on each side).
bool tablesHaveArt(mtgcpp::core::TableScreen &hostTable, mtgcpp::core::TableScreen &guestTable,
                   Match &match);

// A full synced match played while the art cache is live on both sides: the
// procedural front must be replaced by real art as downloads complete.
TEST(Soak, FullSyncedMatchRendersRealArtOnBothSides) {
  // RAII temp dirs for the two art caches.
  class TempDir {
  public:
    TempDir() {
      const std::string unique =
          std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
      path_ = std::filesystem::temp_directory_path() / ("mtgcpp_soak_" + unique);
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

  TempDir hostArtDir;
  TempDir guestArtDir;
  const sf::Vector2u kArtSize{240u, 335u};

  auto png = [] {
    sf::Image image;
    image.create(64u, 64u, sf::Color::Red);
    std::vector<sf::Uint8> raw;
    EXPECT_TRUE(image.saveToMemory(raw, "png"));
    std::vector<std::byte> bytes(raw.size());
    std::memcpy(bytes.data(), raw.data(), raw.size());
    return bytes;
  };

  mtgcpp::core::ArtCache hostCache(hostArtDir.path() / "art", kArtSize);
  mtgcpp::core::ArtCache guestCache(guestArtDir.path() / "art", kArtSize);
  auto fetcher = [&png](const std::string &) { return std::make_optional(png()); };
  hostCache.setFetcher(fetcher);
  guestCache.setFetcher(fetcher);

  Match match;
  ASSERT_TRUE(match.join());
  ASSERT_TRUE(match.chooseDecks());

  mtgcpp::core::TableScreen hostTable;
  mtgcpp::core::TableScreen guestTable;
  hostTable.setArtCache(&hostCache);
  guestTable.setArtCache(&guestCache);

  // Drive a few synced moves while pumping the tables + art caches.
  const std::string h1 = seatOf(match.host.session, PlayerSeat::Host).hand.at(0).id;
  match.host.session.applyLocalAction(tapCard(PlayerSeat::Host, h1));
  for (int i = 0; i < 200 && !tablesHaveArt(hostTable, guestTable, match); ++i) {
    pump(match.server, match.peers);
    hostTable.setBoard(match.host.session.board());
    hostTable.setRole(PlayerSeat::Host);
    hostTable.relayout({0.f, 0.f, 960.f, 600.f}, 1.f);
    hostTable.pumpArt();
    guestTable.setBoard(match.guest.session.board());
    guestTable.setRole(PlayerSeat::Guest);
    guestTable.relayout({0.f, 0.f, 960.f, 600.f}, 1.f);
    guestTable.pumpArt();
    std::this_thread::sleep_for(1ms);
  }
  EXPECT_TRUE(tablesHaveArt(hostTable, guestTable, match));

  match.host.session.leave();
  match.guest.session.leave();
}

// True once BOTH tables resolved a real art texture for some face-up card (the
// procedural fallback is replaced on each side).
bool tablesHaveArt(mtgcpp::core::TableScreen &hostTable, mtgcpp::core::TableScreen &guestTable,
                   Match &match) {
  const SeatBoard &hostSeat = seatOf(match.host.session, PlayerSeat::Host);
  return std::ranges::any_of(hostSeat.hand, [&](const mtgcpp::core::BoardCard &card) {
    return hostTable.artTextureFor(card) != nullptr && guestTable.artTextureFor(card) != nullptr;
  });
}

} // namespace
} // namespace mtgcpp::state
