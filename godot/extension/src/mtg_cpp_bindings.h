// Godot bindings for the mtg_cpp engine (Phase 2).
//
// Each class wraps one engine value/type and exposes it to GDScript. The
// boundary rules:
//   * no C++ exception crosses into Godot — every fallible call is caught and
//     reported as a `Dictionary {ok: bool, error: String, ...}` (see MtgcppDeckRepository);
//   * only the public engine header (`mtg_cpp/mtg_cpp.h`) is included — no asio,
//     no nlohmann, no SFML;
//   * all classes are `RefCounted`, so Godot owns their lifetime via Ref<>.
#pragma once

#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/variant/typed_array.hpp>

#include <memory>
#include <mutex>

#include "core/import_preview.h"
#include "mtg_cpp/mtg_cpp.h"

namespace godot {
class Dictionary;
class String;
} // namespace godot

// Forward declarations for the networking pImpl-style members (defined in the
// .cpp, where their full headers — including asio — are included). Keeping them
// forward-declared here avoids pulling asio into this binding header.
namespace mtgcpp::net {
class AsioTransport;
class Server;
} // namespace mtgcpp::net

// --- MtgcppCard: a resolved card printing -----------------------------------
class MtgcppCard : public godot::RefCounted {
  GDCLASS(MtgcppCard, godot::RefCounted)

public:
  static godot::Ref<MtgcppCard> from(const mtgcpp::core::Card &card);
  mtgcpp::core::Card to() const;

  godot::String get_name() const;
  godot::String get_scryfall_id() const;
  godot::String get_set_code() const;
  godot::String get_set_name() const;
  godot::String get_collector_number() const;
  godot::String get_type_line() const;
  godot::String get_mana_cost() const;
  godot::String get_image_url() const;
  double get_cmc() const;
  godot::PackedStringArray get_colors() const;
  int64_t get_quantity() const;
  // Stable identity of this printing (core::cardKey) — the deck editor's row id.
  godot::String key() const;

protected:
  static void _bind_methods();

private:
  mtgcpp::core::Card card_;
};

// --- MtgcppDeck: a deck of cards --------------------------------------------
class MtgcppDeck : public godot::RefCounted {
  GDCLASS(MtgcppDeck, godot::RefCounted)

public:
  static godot::Ref<MtgcppDeck> from(const mtgcpp::core::Deck &deck);
  mtgcpp::core::Deck to() const;

  godot::String get_name() const;
  void set_name(const godot::String &name);
  godot::String get_format() const;
  void set_format(const godot::String &format);
  godot::String get_id() const;
  int64_t get_total_cards() const;
  int64_t get_unique_cards() const;
  godot::TypedArray<MtgcppCard> get_cards() const;
  void set_cards(const godot::TypedArray<MtgcppCard> &cards);

protected:
  static void _bind_methods();

private:
  mtgcpp::core::Deck deck_;
};

// --- MtgcppDataPaths: the injectable data layout ----------------------------
class MtgcppDataPaths : public godot::RefCounted {
  GDCLASS(MtgcppDataPaths, godot::RefCounted)

public:
  static godot::Ref<MtgcppDataPaths> from_root(const godot::String &root);
  static godot::Ref<MtgcppDataPaths> default_app();

  godot::String get_root() const;
  godot::String card_db_cache() const;
  godot::String decks_dir() const;
  godot::String profile_file() const;
  godot::String art_cache_dir() const;

protected:
  static void _bind_methods();

private:
  mtgcpp::core::DataPaths paths_;
};

// --- MtgcppCardDatabase: local card search + load ---------------------------
class MtgcppCardDatabase : public godot::RefCounted {
  GDCLASS(MtgcppCardDatabase, godot::RefCounted)

public:
  MtgcppCardDatabase();

  // { loaded: int, rejected: int } — synchronous (blocks the caller; prefer
  // load_from_file_async for the real ~600 MB DB).
  godot::Dictionary load_from_file(const godot::String &jsonl_path,
                                   const godot::String &cache_path);

  // Load on a worker thread, never blocking the main thread (UX rule: the UI
  // stays responsive during the multi-minute first parse). Emits
  // `load_finished(ok: bool, error: String, loaded: int, rejected: int)` on the
  // main thread when done.
  void load_from_file_async(const godot::String &jsonl_path, const godot::String &cache_path);

  godot::TypedArray<MtgcppCard> search(const godot::String &query, int64_t limit);
  int64_t size() const;
  bool is_empty() const;

  // Resolve Arena deck text against the loaded database.
  // { ok: bool, error: String, preview: MtgcppImportPreview } — ok=false when the
  // text has no valid card lines (DeckParseError).
  godot::Dictionary import_deck(const godot::String &arena_text);

protected:
  static void _bind_methods();

private:
  // Merges a finished background parse into db_ and emits load_finished. Runs
  // on the main thread via call_deferred (single-owner: only the main thread
  // mutates db_).
  void _commit_loaded();

  mtgcpp::core::CardDatabase db_;

  // Background-load state. The worker thread parses into a temp database and
  // publishes the outcome under load_mutex_; _commit_loaded() then moves it
  // into db_. Guarded so the worker's writes are visible to the main thread.
  std::mutex load_mutex_;
  std::shared_ptr<mtgcpp::core::CardDatabase> pending_;
  int64_t pending_loaded_ = 0;
  int64_t pending_rejected_ = 0;
  std::string pending_error_;
};

// --- MtgcppDeckRepository: persistent deck store ----------------------------
class MtgcppDeckRepository : public godot::RefCounted {
  GDCLASS(MtgcppDeckRepository, godot::RefCounted)

public:
  static godot::Ref<MtgcppDeckRepository> open(const godot::String &root);

  // { ok: bool, error: String, deck: MtgcppDeck }
  godot::Dictionary create(const godot::Ref<MtgcppDeck> &deck);
  // { ok: bool, error: String, deck: MtgcppDeck }
  godot::Dictionary read(const godot::String &id);
  godot::TypedArray<MtgcppDeck> list();
  // Newest-first summaries { id, name, format, total_cards, unique_cards,
  // preview_image, created_at, updated_at } for the vault listing.
  godot::Array list_summaries();
  // { ok: bool, error: String } — replace an existing deck (preserves id).
  godot::Dictionary update(const godot::Ref<MtgcppDeck> &deck);
  bool remove(const godot::String &id);

protected:
  static void _bind_methods();

private:
  std::shared_ptr<mtgcpp::core::DeckRepository> repo_;
};

// --- MtgcppBoardState: whole-table state + reducer --------------------------
class MtgcppBoardState : public godot::RefCounted {
  GDCLASS(MtgcppBoardState, godot::RefCounted)

public:
  static godot::Ref<MtgcppBoardState> initial();

  int64_t life(int64_t seat) const;

protected:
  static void _bind_methods();

private:
  mtgcpp::state::BoardState state_;
};

// --- MtgcppProfiles: the fixed player identities ----------------------------
class MtgcppProfiles : public godot::RefCounted {
  GDCLASS(MtgcppProfiles, godot::RefCounted)

public:
  // [{ id: String, name: String }, ...] in fixed order.
  static godot::Array list();

protected:
  static void _bind_methods();
};

// --- MtgcppProfileStore: persisted player selection -------------------------
class MtgcppProfileStore : public godot::RefCounted {
  GDCLASS(MtgcppProfileStore, godot::RefCounted)

public:
  // Empty string = no selection. `root` is a Godot path (res://, user://, ...).
  static godot::String load(const godot::String &root);
  static void save(const godot::String &root, const godot::String &player_id);
  static void clear(const godot::String &root);

protected:
  static void _bind_methods();
};

// --- MtgcppDeckEditor: pure deck-editing operations -------------------------
class MtgcppDeckEditor : public godot::RefCounted {
  GDCLASS(MtgcppDeckEditor, godot::RefCounted)

public:
  static godot::TypedArray<MtgcppCard> add_card(const godot::TypedArray<MtgcppCard> &cards,
                                                const godot::Ref<MtgcppCard> &card);
  static godot::TypedArray<MtgcppCard> remove_card(const godot::TypedArray<MtgcppCard> &cards,
                                                   const godot::String &key);
  static godot::TypedArray<MtgcppCard> set_quantity(const godot::TypedArray<MtgcppCard> &cards,
                                                    const godot::String &key, int64_t quantity);
  // { total: int, unique: int }
  static godot::Dictionary totals(const godot::TypedArray<MtgcppCard> &cards);

protected:
  static void _bind_methods();
};

// --- MtgcppImportPreview: guarded Arena import state machine ----------------
class MtgcppImportPreview : public godot::RefCounted {
  GDCLASS(MtgcppImportPreview, godot::RefCounted)

public:
  static godot::Ref<MtgcppImportPreview> make(const mtgcpp::core::DeckParseResult &result);

  godot::TypedArray<MtgcppCard> resolved() const;
  // [{ name, set, number, quantity, section }] — the flagged (unresolved) lines.
  godot::Array missing() const;
  bool has_active() const;
  void select_missing(int64_t index);
  void replace_missing(const godot::Ref<MtgcppCard> &card);
  void remove_missing(int64_t index);
  void dismiss();
  bool can_confirm() const;
  // Empty when not confirmable (flagged cards remain).
  godot::TypedArray<MtgcppCard> confirm();

protected:
  static void _bind_methods();

private:
  mtgcpp::core::ImportPreview preview_;
};

// --- MtgcppArena: Arena text formatting -------------------------------------
class MtgcppArena : public godot::RefCounted {
  GDCLASS(MtgcppArena, godot::RefCounted)

public:
  // Render cards as Arena export text (cards without a printing are dropped).
  static godot::String to_text(const godot::TypedArray<MtgcppCard> &cards);

protected:
  static void _bind_methods();
};

// --- MtgcppSession: the room session (host relay + client + state machine) --
//
// One MtgcppSession is one peer at the table. It owns the embedded relay (host
// side), the TCP client, and the state::Session. Every method runs on the main
// thread; the background threads (Asio) only fill queues that pump() drains.
class MtgcppSession : public godot::RefCounted {
  GDCLASS(MtgcppSession, godot::RefCounted)

public:
  MtgcppSession();
  ~MtgcppSession();

  // Host: start the embedded relay, join it locally, share the address.
  // { ok: bool, error: String, address: String }
  godot::Dictionary create_room();
  // Guest: connect to `IP:PORT`.
  // { ok: bool, error: String }
  godot::Dictionary join(const godot::String &address);
  void leave();

  // Drain the relay (host) + the session's inbound frames. Call every frame.
  void pump();

  // --- session introspection ------------------------------------------------
  bool connected() const;
  // 0 = Idle, 1 = Waiting, 2 = Ready.
  int64_t status() const;
  // -1 = none, 0 = host, 1 = guest.
  int64_t role() const;
  godot::String player_id() const;
  godot::Array players() const;
  godot::String share_address() const;
  godot::String my_deck_name() const;
  godot::String their_deck_name() const;
  bool can_start_table() const;
  godot::String last_error() const;

  // --- deck ----------------------------------------------------------------
  // { ok: bool, error: String }
  godot::Dictionary choose_deck(const godot::Ref<MtgcppDeck> &deck);

  // --- board view (seat 0/1, zone 0..4) ------------------------------------
  godot::Array hand(int64_t seat) const;
  godot::Array zone_cards(int64_t seat, int64_t zone_index) const;
  godot::Array stack_cards() const;
  int64_t life(int64_t seat) const;

  // --- actions (build the BoardAction and apply it locally + sync) ----------
  void tap(int64_t seat, const godot::String &card_id);
  void add_counter(int64_t seat, const godot::String &card_id);
  void flip(int64_t seat, const godot::String &card_id);
  void create_token(int64_t seat, int64_t zone_index, const godot::String &name);
  void move_to_zone(int64_t seat, const godot::String &card_id, int64_t zone_index);
  void move_to_stack(const godot::String &card_id);
  void set_life(int64_t seat, int64_t life);

  // --- sandbox library (local draw/play) ------------------------------------
  void enter_sandbox();
  bool is_sandbox() const;
  // Remaining library cards as board-card dictionaries.
  godot::Array library() const;
  void draw_card();
  void play_card(const godot::String &card_id);
  void place_from_library(const godot::String &card_id);

  // --- hand reveal ----------------------------------------------------------
  void request_hand_reveal();
  // Sends the local hand (projected by the engine) to the opponent.
  void accept_hand_reveal();
  void deny_hand_reveal();
  void dismiss_reveal_prompt();
  void clear_revealed();
  godot::String reveal_request_from() const;
  bool reveal_accepted() const;
  godot::Array revealed_hand() const;

protected:
  static void _bind_methods();

private:
  static int seat_index(std::optional<mtgcpp::core::PlayerSeat> seat);

  std::unique_ptr<mtgcpp::net::AsioTransport> transport_;
  std::unique_ptr<mtgcpp::net::Server> server_;
  std::unique_ptr<mtgcpp::net::Client> client_;
  std::unique_ptr<mtgcpp::state::Session> session_;
  std::string share_address_;
};
