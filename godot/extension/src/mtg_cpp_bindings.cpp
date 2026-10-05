// Godot bindings implementation (Phase 2). See mtg_cpp_bindings.h for the
// boundary contract. Every fallible engine call is caught and turned into a
// Godot result Dictionary, so a C++ exception can never cross the boundary.

#include "mtg_cpp_bindings.h"

#include "core/arena.h"
#include "core/deck_editor.h"
#include "core/deck_parser.h"
#include "core/importer.h"
#include "core/import_preview.h"
#include "net/address.h"
#include "net/server.h"
#include "net/transport.h"

#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/callable_method_pointer.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include <cstdint>
#include <exception>
#include <filesystem>
#include <string>
#include <thread>
#include <utility>
#include <variant>
#include <vector>

using namespace godot;

namespace {

// The engine stores text as UTF-8; card data is full of non-ASCII (the em dash
// in "Creature — Human"). Convert through explicit UTF-8 on both sides.
std::string to_std(const godot::String &s) { return std::string(s.utf8().get_data()); }

godot::String to_gd(std::string_view s) {
  return godot::String::utf8(s.data(), static_cast<int>(s.size()));
}

// Convert a Godot path (res://, user://, or absolute) to a real OS path the C++
// engine can open. Godot's resource paths are virtual; the engine deals in
// filesystem paths, so globalize at the boundary.
std::filesystem::path to_os_path(const godot::String &s) {
  return std::filesystem::path(
      to_std(godot::ProjectSettings::get_singleton()->globalize_path(s)));
}

// The shared result shape: { ok: bool, error: String, ... }. A failure always
// has ok=false and a human-readable error; a success has ok=true and its
// payload under a documented key.
godot::Dictionary ok(godot::Dictionary extra) {
  extra["ok"] = true;
  extra["error"] = godot::String();
  return extra;
}

godot::Dictionary fail(std::string_view message) {
  godot::Dictionary d;
  d["ok"] = false;
  d["error"] = to_gd(message);
  return d;
}

} // namespace

// ---------------------------------------------------------------------------
// MtgcppCard
// ---------------------------------------------------------------------------

godot::Ref<MtgcppCard> MtgcppCard::from(const mtgcpp::core::Card &card) {
  godot::Ref<MtgcppCard> obj;
  obj.instantiate();
  obj->card_ = card;
  return obj;
}

mtgcpp::core::Card MtgcppCard::to() const { return card_; }

godot::String MtgcppCard::get_name() const { return to_gd(card_.name); }
godot::String MtgcppCard::get_scryfall_id() const { return to_gd(card_.scryfall_id); }
godot::String MtgcppCard::get_set_code() const { return to_gd(card_.set_code); }
godot::String MtgcppCard::get_set_name() const { return to_gd(card_.set_name); }
godot::String MtgcppCard::get_collector_number() const { return to_gd(card_.collector_number); }
godot::String MtgcppCard::get_type_line() const { return to_gd(card_.type_line); }
godot::String MtgcppCard::get_mana_cost() const { return to_gd(card_.mana_cost); }
godot::String MtgcppCard::get_image_url() const { return to_gd(mtgcpp::core::cardImage(card_)); }
double MtgcppCard::get_cmc() const { return card_.cmc.value_or(0.0); }
int64_t MtgcppCard::get_quantity() const { return card_.quantity; }
godot::String MtgcppCard::key() const { return to_gd(mtgcpp::core::cardKey(card_)); }

godot::PackedStringArray MtgcppCard::get_colors() const {
  godot::PackedStringArray out;
  for (const std::string &color : card_.colors) {
    out.append(to_gd(color));
  }
  return out;
}

void MtgcppCard::_bind_methods() {
  ClassDB::bind_method(D_METHOD("get_name"), &MtgcppCard::get_name);
  ClassDB::bind_method(D_METHOD("get_scryfall_id"), &MtgcppCard::get_scryfall_id);
  ClassDB::bind_method(D_METHOD("get_set_code"), &MtgcppCard::get_set_code);
  ClassDB::bind_method(D_METHOD("get_set_name"), &MtgcppCard::get_set_name);
  ClassDB::bind_method(D_METHOD("get_collector_number"), &MtgcppCard::get_collector_number);
  ClassDB::bind_method(D_METHOD("get_type_line"), &MtgcppCard::get_type_line);
  ClassDB::bind_method(D_METHOD("get_mana_cost"), &MtgcppCard::get_mana_cost);
  ClassDB::bind_method(D_METHOD("get_image_url"), &MtgcppCard::get_image_url);
  ClassDB::bind_method(D_METHOD("get_cmc"), &MtgcppCard::get_cmc);
  ClassDB::bind_method(D_METHOD("get_colors"), &MtgcppCard::get_colors);
  ClassDB::bind_method(D_METHOD("get_quantity"), &MtgcppCard::get_quantity);
  ClassDB::bind_method(D_METHOD("key"), &MtgcppCard::key);

  ClassDB::add_property("MtgcppCard", PropertyInfo(Variant::STRING, "name"), "", "get_name");
  ClassDB::add_property("MtgcppCard", PropertyInfo(Variant::STRING, "scryfall_id"), "",
                        "get_scryfall_id");
  ClassDB::add_property("MtgcppCard", PropertyInfo(Variant::STRING, "set_code"), "",
                        "get_set_code");
  ClassDB::add_property("MtgcppCard", PropertyInfo(Variant::STRING, "collector_number"), "",
                        "get_collector_number");
  ClassDB::add_property("MtgcppCard", PropertyInfo(Variant::STRING, "type_line"), "",
                        "get_type_line");
  ClassDB::add_property("MtgcppCard", PropertyInfo(Variant::INT, "quantity"), "",
                        "get_quantity");
}

// ---------------------------------------------------------------------------
// MtgcppDeck
// ---------------------------------------------------------------------------

godot::Ref<MtgcppDeck> MtgcppDeck::from(const mtgcpp::core::Deck &deck) {
  godot::Ref<MtgcppDeck> obj;
  obj.instantiate();
  obj->deck_ = deck;
  return obj;
}

mtgcpp::core::Deck MtgcppDeck::to() const { return deck_; }

godot::String MtgcppDeck::get_name() const { return to_gd(deck_.name); }
void MtgcppDeck::set_name(const godot::String &name) { deck_.name = to_std(name); }
godot::String MtgcppDeck::get_format() const { return to_gd(deck_.format); }
void MtgcppDeck::set_format(const godot::String &format) { deck_.format = to_std(format); }
godot::String MtgcppDeck::get_id() const { return to_gd(deck_.id); }
int64_t MtgcppDeck::get_total_cards() const { return deck_.total_cards; }
int64_t MtgcppDeck::get_unique_cards() const { return deck_.unique_cards; }

godot::TypedArray<MtgcppCard> MtgcppDeck::get_cards() const {
  godot::TypedArray<MtgcppCard> out;
  for (const mtgcpp::core::Card &card : deck_.cards) {
    out.append(MtgcppCard::from(card));
  }
  return out;
}

void MtgcppDeck::set_cards(const godot::TypedArray<MtgcppCard> &cards) {
  deck_.cards.clear();
  deck_.cards.reserve(cards.size());
  for (int64_t i = 0; i < cards.size(); ++i) {
    const godot::Ref<MtgcppCard> card = cards[i];
    if (card.is_valid()) {
      deck_.cards.push_back(card->to());
    }
  }
  // Keep the summary fields consistent with the cards, like the SFML App did
  // before saving (deckTotalCards/deckUniqueCards).
  deck_.total_cards = mtgcpp::core::deckTotalCards(deck_.cards);
  deck_.unique_cards = mtgcpp::core::deckUniqueCards(deck_.cards);
}

void MtgcppDeck::_bind_methods() {
  ClassDB::bind_method(D_METHOD("get_name"), &MtgcppDeck::get_name);
  ClassDB::bind_method(D_METHOD("set_name", "name"), &MtgcppDeck::set_name);
  ClassDB::bind_method(D_METHOD("get_format"), &MtgcppDeck::get_format);
  ClassDB::bind_method(D_METHOD("set_format", "format"), &MtgcppDeck::set_format);
  ClassDB::bind_method(D_METHOD("get_id"), &MtgcppDeck::get_id);
  ClassDB::bind_method(D_METHOD("get_total_cards"), &MtgcppDeck::get_total_cards);
  ClassDB::bind_method(D_METHOD("get_unique_cards"), &MtgcppDeck::get_unique_cards);
  ClassDB::bind_method(D_METHOD("get_cards"), &MtgcppDeck::get_cards);
  ClassDB::bind_method(D_METHOD("set_cards", "cards"), &MtgcppDeck::set_cards);

  ClassDB::add_property("MtgcppDeck", PropertyInfo(Variant::STRING, "name"), "set_name",
                        "get_name");
  ClassDB::add_property("MtgcppDeck", PropertyInfo(Variant::STRING, "format"), "set_format",
                        "get_format");
  ClassDB::add_property("MtgcppDeck", PropertyInfo(Variant::STRING, "id"), "", "get_id");
}

// ---------------------------------------------------------------------------
// MtgcppDataPaths
// ---------------------------------------------------------------------------

godot::Ref<MtgcppDataPaths> MtgcppDataPaths::from_root(const godot::String &root) {
  godot::Ref<MtgcppDataPaths> obj;
  obj.instantiate();
  obj->paths_ = mtgcpp::core::DataPaths::fromRoot(to_os_path(root));
  return obj;
}

godot::Ref<MtgcppDataPaths> MtgcppDataPaths::default_app() {
  godot::Ref<MtgcppDataPaths> obj;
  obj.instantiate();
  obj->paths_ = mtgcpp::core::DataPaths::defaultApp();
  return obj;
}

godot::String MtgcppDataPaths::get_root() const { return to_gd(paths_.root.string()); }
godot::String MtgcppDataPaths::card_db_cache() const { return to_gd(paths_.cardDbCache().string()); }
godot::String MtgcppDataPaths::decks_dir() const { return to_gd(paths_.decksDir().string()); }
godot::String MtgcppDataPaths::profile_file() const { return to_gd(paths_.profileFile().string()); }
godot::String MtgcppDataPaths::art_cache_dir() const {
  return to_gd(paths_.artCacheDir().string());
}

void MtgcppDataPaths::_bind_methods() {
  ClassDB::bind_static_method("MtgcppDataPaths", D_METHOD("from_root", "root"),
                              &MtgcppDataPaths::from_root);
  ClassDB::bind_static_method("MtgcppDataPaths", D_METHOD("default_app"),
                              &MtgcppDataPaths::default_app);

  ClassDB::bind_method(D_METHOD("get_root"), &MtgcppDataPaths::get_root);
  ClassDB::bind_method(D_METHOD("card_db_cache"), &MtgcppDataPaths::card_db_cache);
  ClassDB::bind_method(D_METHOD("decks_dir"), &MtgcppDataPaths::decks_dir);
  ClassDB::bind_method(D_METHOD("profile_file"), &MtgcppDataPaths::profile_file);
  ClassDB::bind_method(D_METHOD("art_cache_dir"), &MtgcppDataPaths::art_cache_dir);
}

// ---------------------------------------------------------------------------
// MtgcppCardDatabase
// ---------------------------------------------------------------------------

MtgcppCardDatabase::MtgcppCardDatabase() = default;

godot::Dictionary MtgcppCardDatabase::load_from_file(const godot::String &jsonl_path,
                                                     const godot::String &cache_path) {
  try {
    const mtgcpp::core::CardDatabase::LoadResult result =
        db_.loadFromFile(to_os_path(jsonl_path), to_os_path(cache_path));
    godot::Dictionary d;
    d["loaded"] = static_cast<int64_t>(result.loaded);
    d["rejected"] = static_cast<int64_t>(result.rejected);
    return ok(std::move(d));
  } catch (const std::exception &exc) {
    return fail(exc.what());
  }
}

godot::TypedArray<MtgcppCard> MtgcppCardDatabase::search(const godot::String &query,
                                                         int64_t limit) {
  godot::TypedArray<MtgcppCard> out;
  const std::vector<mtgcpp::core::Card> cards =
      db_.search(to_std(query), static_cast<std::size_t>(limit));
  for (const mtgcpp::core::Card &card : cards) {
    out.append(MtgcppCard::from(card));
  }
  return out;
}

void MtgcppCardDatabase::load_from_file_async(const godot::String &jsonl_path,
                                              const godot::String &cache_path) {
  // Capture a Ref to keep this object alive until the worker finishes (and the
  // deferred commit has run). The worker never touches db_ — it parses into a
  // fresh database and publishes it under the mutex.
  const godot::Ref<MtgcppCardDatabase> self(this);
  const std::filesystem::path jsonl = to_os_path(jsonl_path);
  const std::filesystem::path cache = to_os_path(cache_path);
  std::thread([self, jsonl, cache]() {
    auto parsed = std::make_shared<mtgcpp::core::CardDatabase>();
    int64_t loaded = 0;
    int64_t rejected = 0;
    std::string error;
    try {
      const mtgcpp::core::CardDatabase::LoadResult result =
          parsed->loadFromFile(jsonl, cache);
      loaded = static_cast<int64_t>(result.loaded);
      rejected = static_cast<int64_t>(result.rejected);
    } catch (const std::exception &exc) {
      error = exc.what();
    }
    {
      std::lock_guard<std::mutex> lock(self->load_mutex_);
      self->pending_ = std::move(parsed);
      self->pending_loaded_ = loaded;
      self->pending_rejected_ = rejected;
      self->pending_error_ = std::move(error);
    }
    callable_mp(self.ptr(), &MtgcppCardDatabase::_commit_loaded).call_deferred();
  }).detach();
}

void MtgcppCardDatabase::_commit_loaded() {
  std::shared_ptr<mtgcpp::core::CardDatabase> parsed;
  int64_t loaded = 0;
  int64_t rejected = 0;
  std::string error;
  {
    std::lock_guard<std::mutex> lock(load_mutex_);
    parsed = std::move(pending_);
    loaded = pending_loaded_;
    rejected = pending_rejected_;
    error = std::move(pending_error_);
  }
  if (parsed) {
    db_ = std::move(*parsed);
  }
  emit_signal("load_finished", error.empty(), to_gd(error), loaded, rejected);
}

int64_t MtgcppCardDatabase::size() const { return static_cast<int64_t>(db_.size()); }

bool MtgcppCardDatabase::is_empty() const { return db_.empty(); }

godot::Dictionary MtgcppCardDatabase::import_deck(const godot::String &arena_text) {
  try {
    const mtgcpp::core::DeckParseResult result = mtgcpp::core::importDeck(to_std(arena_text), db_);
    godot::Dictionary d;
    d["preview"] = MtgcppImportPreview::make(result);
    return ok(std::move(d));
  } catch (const std::exception &exc) {
    return fail(exc.what());
  }
}

void MtgcppCardDatabase::_bind_methods() {
  ClassDB::bind_method(D_METHOD("load_from_file", "jsonl_path", "cache_path"),
                       &MtgcppCardDatabase::load_from_file);
  ClassDB::bind_method(D_METHOD("load_from_file_async", "jsonl_path", "cache_path"),
                       &MtgcppCardDatabase::load_from_file_async);
  ClassDB::bind_method(D_METHOD("search", "query", "limit"), &MtgcppCardDatabase::search,
                       DEFVAL(50));
  ClassDB::bind_method(D_METHOD("size"), &MtgcppCardDatabase::size);
  ClassDB::bind_method(D_METHOD("is_empty"), &MtgcppCardDatabase::is_empty);
  ClassDB::bind_method(D_METHOD("import_deck", "arena_text"), &MtgcppCardDatabase::import_deck);

  ADD_SIGNAL(MethodInfo("load_finished", PropertyInfo(Variant::BOOL, "ok"),
                        PropertyInfo(Variant::STRING, "error"), PropertyInfo(Variant::INT, "loaded"),
                        PropertyInfo(Variant::INT, "rejected")));
}

// ---------------------------------------------------------------------------
// MtgcppDeckRepository
// ---------------------------------------------------------------------------

godot::Ref<MtgcppDeckRepository> MtgcppDeckRepository::open(const godot::String &root) {
  godot::Ref<MtgcppDeckRepository> obj;
  obj.instantiate();
  obj->repo_ = std::make_shared<mtgcpp::core::DeckRepository>(to_os_path(root));
  return obj;
}

godot::Dictionary MtgcppDeckRepository::create(const godot::Ref<MtgcppDeck> &deck) {
  if (deck.is_null()) {
    return fail("create: null deck");
  }
  try {
    const mtgcpp::core::Deck stored = repo_->create(deck->to());
    godot::Dictionary d;
    d["deck"] = MtgcppDeck::from(stored);
    return ok(std::move(d));
  } catch (const std::exception &exc) {
    return fail(exc.what());
  }
}

godot::Dictionary MtgcppDeckRepository::read(const godot::String &id) {
  const std::variant<mtgcpp::core::Deck, mtgcpp::core::DeckReadError> result =
      repo_->read(to_std(id));
  if (std::holds_alternative<mtgcpp::core::Deck>(result)) {
    godot::Dictionary d;
    d["deck"] = MtgcppDeck::from(std::get<mtgcpp::core::Deck>(result));
    return ok(std::move(d));
  }
  const mtgcpp::core::DeckReadError err = std::get<mtgcpp::core::DeckReadError>(result);
  const std::string message =
      err == mtgcpp::core::DeckReadError::NotFound ? "deck not found" : "deck is invalid";
  return fail(message);
}

godot::TypedArray<MtgcppDeck> MtgcppDeckRepository::list() {
  godot::TypedArray<MtgcppDeck> out;
  const std::vector<mtgcpp::core::Deck> decks = repo_->list();
  for (const mtgcpp::core::Deck &deck : decks) {
    out.append(MtgcppDeck::from(deck));
  }
  return out;
}

godot::Array MtgcppDeckRepository::list_summaries() {
  godot::Array out;
  const std::vector<mtgcpp::core::DeckSummary> summaries = repo_->listSummaries();
  for (const mtgcpp::core::DeckSummary &summary : summaries) {
    godot::Dictionary d;
    d["id"] = to_gd(summary.id);
    d["name"] = to_gd(summary.name);
    d["format"] = to_gd(summary.format);
    d["total_cards"] = summary.total_cards;
    d["unique_cards"] = summary.unique_cards;
    d["preview_image"] = summary.preview_image.has_value() ? to_gd(summary.preview_image.value())
                                                          : godot::String();
    d["created_at"] = to_gd(summary.created_at);
    d["updated_at"] = to_gd(summary.updated_at);
    out.append(d);
  }
  return out;
}

godot::Dictionary MtgcppDeckRepository::update(const godot::Ref<MtgcppDeck> &deck) {
  if (deck.is_null()) {
    return fail("update: null deck");
  }
  try {
    if (!repo_->update(deck->to())) {
      return fail("update: deck not found or invalid");
    }
    return ok(godot::Dictionary{});
  } catch (const std::exception &exc) {
    return fail(exc.what());
  }
}

bool MtgcppDeckRepository::remove(const godot::String &id) { return repo_->remove(to_std(id)); }

void MtgcppDeckRepository::_bind_methods() {
  ClassDB::bind_static_method("MtgcppDeckRepository", D_METHOD("open", "root"),
                              &MtgcppDeckRepository::open);
  ClassDB::bind_method(D_METHOD("create", "deck"), &MtgcppDeckRepository::create);
  ClassDB::bind_method(D_METHOD("read", "id"), &MtgcppDeckRepository::read);
  ClassDB::bind_method(D_METHOD("list"), &MtgcppDeckRepository::list);
  ClassDB::bind_method(D_METHOD("list_summaries"), &MtgcppDeckRepository::list_summaries);
  ClassDB::bind_method(D_METHOD("update", "deck"), &MtgcppDeckRepository::update);
  ClassDB::bind_method(D_METHOD("remove", "id"), &MtgcppDeckRepository::remove);
}

// ---------------------------------------------------------------------------
// MtgcppBoardState
// ---------------------------------------------------------------------------

godot::Ref<MtgcppBoardState> MtgcppBoardState::initial() {
  godot::Ref<MtgcppBoardState> obj;
  obj.instantiate();
  obj->state_ = mtgcpp::state::initialBoardState();
  return obj;
}

int64_t MtgcppBoardState::life(int64_t seat) const {
  const std::size_t index = static_cast<std::size_t>(seat);
  if (index >= state_.life.size()) {
    return 0;
  }
  return state_.life[index];
}

void MtgcppBoardState::_bind_methods() {
  ClassDB::bind_static_method("MtgcppBoardState", D_METHOD("initial"),
                              &MtgcppBoardState::initial);
  ClassDB::bind_method(D_METHOD("life", "seat"), &MtgcppBoardState::life);
}

// ---------------------------------------------------------------------------
// MtgcppProfiles / MtgcppProfileStore
// ---------------------------------------------------------------------------

godot::Array MtgcppProfiles::list() {
  godot::Array out;
  for (const mtgcpp::core::PlayerProfile &profile : mtgcpp::core::playerProfiles()) {
    godot::Dictionary d;
    d["id"] = to_gd(profile.id);
    d["name"] = to_gd(profile.name);
    out.append(d);
  }
  return out;
}

void MtgcppProfiles::_bind_methods() {
  ClassDB::bind_static_method("MtgcppProfiles", D_METHOD("list"), &MtgcppProfiles::list);
}

godot::String MtgcppProfileStore::load(const godot::String &root) {
  const std::optional<std::string> id =
      mtgcpp::core::loadPlayerId(to_os_path(root));
  return id.has_value() ? to_gd(id.value()) : godot::String();
}

void MtgcppProfileStore::save(const godot::String &root, const godot::String &player_id) {
  try {
    mtgcpp::core::savePlayerId(to_os_path(root), to_std(player_id));
  } catch (const std::exception &exc) {
    godot::UtilityFunctions::printerr("MtgcppProfileStore.save: ", exc.what());
  }
}

void MtgcppProfileStore::clear(const godot::String &root) {
  mtgcpp::core::clearPlayerId(to_os_path(root));
}

void MtgcppProfileStore::_bind_methods() {
  ClassDB::bind_static_method("MtgcppProfileStore", D_METHOD("load", "root"),
                              &MtgcppProfileStore::load);
  ClassDB::bind_static_method("MtgcppProfileStore", D_METHOD("save", "root", "player_id"),
                              &MtgcppProfileStore::save);
  ClassDB::bind_static_method("MtgcppProfileStore", D_METHOD("clear", "root"),
                              &MtgcppProfileStore::clear);
}

// ---------------------------------------------------------------------------
// MtgcppDeckEditor (pure ops over TypedArray<MtgcppCard>)
// ---------------------------------------------------------------------------

namespace {

std::vector<mtgcpp::core::Card> to_cards(const godot::TypedArray<MtgcppCard> &cards) {
  std::vector<mtgcpp::core::Card> out;
  out.reserve(cards.size());
  for (int64_t i = 0; i < cards.size(); ++i) {
    const godot::Ref<MtgcppCard> card = cards[i];
    if (card.is_valid()) {
      out.push_back(card->to());
    }
  }
  return out;
}

godot::TypedArray<MtgcppCard> to_card_array(const std::vector<mtgcpp::core::Card> &cards) {
  godot::TypedArray<MtgcppCard> out;
  for (const mtgcpp::core::Card &card : cards) {
    out.append(MtgcppCard::from(card));
  }
  return out;
}

godot::Dictionary missing_to_dict(const mtgcpp::core::MissingCard &missing) {
  godot::Dictionary d;
  d["name"] = to_gd(missing.name);
  d["set"] = to_gd(missing.set);
  d["number"] = to_gd(missing.number);
  d["quantity"] = missing.quantity;
  d["section"] = to_gd(mtgcpp::core::arenaSectionToString(missing.section));
  return d;
}

} // namespace

godot::TypedArray<MtgcppCard> MtgcppDeckEditor::add_card(const godot::TypedArray<MtgcppCard> &cards,
                                                         const godot::Ref<MtgcppCard> &card) {
  if (card.is_null()) {
    return to_card_array(to_cards(cards));
  }
  return to_card_array(mtgcpp::core::addCardToDeck(to_cards(cards), card->to()));
}

godot::TypedArray<MtgcppCard> MtgcppDeckEditor::remove_card(const godot::TypedArray<MtgcppCard> &cards,
                                                            const godot::String &key) {
  return to_card_array(mtgcpp::core::removeCardFromDeck(to_cards(cards), to_std(key)));
}

godot::TypedArray<MtgcppCard> MtgcppDeckEditor::set_quantity(const godot::TypedArray<MtgcppCard> &cards,
                                                             const godot::String &key,
                                                             int64_t quantity) {
  return to_card_array(mtgcpp::core::setCardQuantity(to_cards(cards), to_std(key),
                                                     static_cast<int>(quantity)));
}

godot::Dictionary MtgcppDeckEditor::totals(const godot::TypedArray<MtgcppCard> &cards) {
  const mtgcpp::core::DeckTotals totals = mtgcpp::core::deckTotals(to_cards(cards));
  godot::Dictionary d;
  d["total"] = totals.total_cards;
  d["unique"] = totals.unique_cards;
  return d;
}

void MtgcppDeckEditor::_bind_methods() {
  ClassDB::bind_static_method("MtgcppDeckEditor", D_METHOD("add_card", "cards", "card"),
                              &MtgcppDeckEditor::add_card);
  ClassDB::bind_static_method("MtgcppDeckEditor", D_METHOD("remove_card", "cards", "key"),
                              &MtgcppDeckEditor::remove_card);
  ClassDB::bind_static_method("MtgcppDeckEditor",
                              D_METHOD("set_quantity", "cards", "key", "quantity"),
                              &MtgcppDeckEditor::set_quantity);
  ClassDB::bind_static_method("MtgcppDeckEditor", D_METHOD("totals", "cards"),
                              &MtgcppDeckEditor::totals);
}

// ---------------------------------------------------------------------------
// MtgcppImportPreview
// ---------------------------------------------------------------------------

godot::Ref<MtgcppImportPreview> MtgcppImportPreview::make(
    const mtgcpp::core::DeckParseResult &result) {
  godot::Ref<MtgcppImportPreview> obj;
  obj.instantiate();
  obj->preview_ = mtgcpp::core::makeImportPreview(result);
  return obj;
}

godot::TypedArray<MtgcppCard> MtgcppImportPreview::resolved() const {
  return to_card_array(preview_.cards);
}

godot::Array MtgcppImportPreview::missing() const {
  godot::Array out;
  for (const mtgcpp::core::MissingCard &missing : preview_.missing) {
    out.append(missing_to_dict(missing));
  }
  return out;
}

bool MtgcppImportPreview::has_active() const { return preview_.activeMissing.has_value(); }

void MtgcppImportPreview::select_missing(int64_t index) {
  const std::size_t i = static_cast<std::size_t>(index);
  if (i >= preview_.missing.size()) {
    return;
  }
  preview_ = mtgcpp::core::selectMissing(preview_, preview_.missing.at(i));
}

void MtgcppImportPreview::replace_missing(const godot::Ref<MtgcppCard> &card) {
  if (card.is_null() || !preview_.activeMissing.has_value()) {
    return;
  }
  const std::string key = mtgcpp::core::missingCardKey(preview_.activeMissing.value());
  preview_ = mtgcpp::core::replaceMissing(preview_, key, card->to());
}

void MtgcppImportPreview::remove_missing(int64_t index) {
  const std::size_t i = static_cast<std::size_t>(index);
  if (i >= preview_.missing.size()) {
    return;
  }
  const std::string key = mtgcpp::core::missingCardKey(preview_.missing.at(i));
  preview_ = mtgcpp::core::removeMissing(preview_, key);
}

void MtgcppImportPreview::dismiss() { preview_ = mtgcpp::core::dismissMissing(preview_); }

bool MtgcppImportPreview::can_confirm() const { return mtgcpp::core::canConfirmImport(preview_); }

godot::TypedArray<MtgcppCard> MtgcppImportPreview::confirm() {
  const std::optional<std::vector<mtgcpp::core::Card>> cards =
      mtgcpp::core::confirmImport(preview_);
  if (!cards.has_value()) {
    return godot::TypedArray<MtgcppCard>{};
  }
  return to_card_array(cards.value());
}

void MtgcppImportPreview::_bind_methods() {
  ClassDB::bind_method(D_METHOD("resolved"), &MtgcppImportPreview::resolved);
  ClassDB::bind_method(D_METHOD("missing"), &MtgcppImportPreview::missing);
  ClassDB::bind_method(D_METHOD("has_active"), &MtgcppImportPreview::has_active);
  ClassDB::bind_method(D_METHOD("select_missing", "index"), &MtgcppImportPreview::select_missing);
  ClassDB::bind_method(D_METHOD("replace_missing", "card"), &MtgcppImportPreview::replace_missing);
  ClassDB::bind_method(D_METHOD("remove_missing", "index"), &MtgcppImportPreview::remove_missing);
  ClassDB::bind_method(D_METHOD("dismiss"), &MtgcppImportPreview::dismiss);
  ClassDB::bind_method(D_METHOD("can_confirm"), &MtgcppImportPreview::can_confirm);
  ClassDB::bind_method(D_METHOD("confirm"), &MtgcppImportPreview::confirm);
}

// ---------------------------------------------------------------------------
// MtgcppArena
// ---------------------------------------------------------------------------

godot::String MtgcppArena::to_text(const godot::TypedArray<MtgcppCard> &cards) {
  return to_gd(mtgcpp::core::toArenaText(to_cards(cards)));
}

void MtgcppArena::_bind_methods() {
  ClassDB::bind_static_method("MtgcppArena", D_METHOD("to_text", "cards"), &MtgcppArena::to_text);
}

// ---------------------------------------------------------------------------
// MtgcppSession
// ---------------------------------------------------------------------------

namespace {

godot::Dictionary board_card_to_dict(const mtgcpp::core::BoardCard &card) {
  godot::Dictionary d;
  d["id"] = to_gd(card.id);
  d["scryfall_id"] = to_gd(card.scryfall_id);
  d["name"] = to_gd(card.name);
  d["mana_cost"] = to_gd(card.mana_cost);
  d["type_line"] = to_gd(card.type_line);
  d["image_url"] = to_gd(card.image_url);
  d["tapped"] = card.tapped;
  d["counters"] = card.counters;
  d["flipped"] = card.flipped;
  d["is_token"] = card.is_token;
  return d;
}

godot::Array board_cards_to_array(const std::vector<mtgcpp::core::BoardCard> &cards) {
  godot::Array out;
  for (const mtgcpp::core::BoardCard &card : cards) {
    out.append(board_card_to_dict(card));
  }
  return out;
}

int session_status_index(mtgcpp::state::Session::RoomStatus status) {
  switch (status) {
  case mtgcpp::state::Session::RoomStatus::Idle:
    return 0;
  case mtgcpp::state::Session::RoomStatus::Waiting:
    return 1;
  case mtgcpp::state::Session::RoomStatus::Ready:
    return 2;
  }
  return 0;
}

} // namespace

MtgcppSession::MtgcppSession() = default;

MtgcppSession::~MtgcppSession() { leave(); }

int MtgcppSession::seat_index(std::optional<mtgcpp::core::PlayerSeat> seat) {
  return seat.has_value() ? static_cast<int>(mtgcpp::state::seatIndex(seat.value())) : -1;
}

godot::Dictionary MtgcppSession::create_room() {
  leave();
  try {
    transport_ = std::make_unique<mtgcpp::net::AsioTransport>(0); // ephemeral port
    transport_->start();
    server_ = std::make_unique<mtgcpp::net::Server>(*transport_);
    server_->start();
    const std::string port = std::to_string(transport_->localPort());
    share_address_ = mtgcpp::net::localIpAddress() + ":" + port;
    client_ = std::make_unique<mtgcpp::net::Client>();
    if (!client_->connect("127.0.0.1", port)) {
      leave();
      return fail("Could not connect to the local relay");
    }
    session_ = std::make_unique<mtgcpp::state::Session>(*client_);
    godot::Dictionary d;
    d["address"] = to_gd(share_address_);
    return ok(std::move(d));
  } catch (const std::exception &exc) {
    leave();
    return fail(exc.what());
  }
}

godot::Dictionary MtgcppSession::join(const godot::String &address) {
  leave();
  const std::string addr = to_std(address);
  const std::size_t colon = addr.rfind(':');
  if (colon == std::string::npos || colon == 0 || colon + 1 >= addr.size()) {
    return fail("Enter the address as IP:PORT, e.g. 192.168.1.5:7500");
  }
  const std::string host = addr.substr(0, colon);
  const std::string port = addr.substr(colon + 1);
  client_ = std::make_unique<mtgcpp::net::Client>();
  if (!client_->connect(host, port)) {
    client_.reset();
    return fail("Could not connect to " + addr);
  }
  session_ = std::make_unique<mtgcpp::state::Session>(*client_);
  return ok(godot::Dictionary{});
}

void MtgcppSession::leave() {
  if (session_) {
    session_->leave();
  }
  session_.reset();
  client_.reset();
  server_.reset();
  transport_.reset();
  share_address_.clear();
}

void MtgcppSession::pump() {
  if (!session_) {
    return;
  }
  if (server_) {
    server_->runOnce();
  }
  session_->drain();
}

bool MtgcppSession::connected() const { return session_ && session_->connected(); }

int64_t MtgcppSession::status() const {
  return session_ ? session_status_index(session_->status()) : 0;
}

int64_t MtgcppSession::role() const { return session_ ? seat_index(session_->role()) : -1; }

godot::String MtgcppSession::player_id() const {
  return session_ && session_->playerId().has_value() ? to_gd(session_->playerId().value())
                                                      : godot::String();
}

godot::Array MtgcppSession::players() const {
  godot::Array out;
  if (session_) {
    for (const std::string &id : session_->players()) {
      out.append(to_gd(id));
    }
  }
  return out;
}

godot::String MtgcppSession::share_address() const { return to_gd(share_address_); }

godot::String MtgcppSession::my_deck_name() const {
  if (session_ && session_->board().my_deck.has_value()) {
    return to_gd(session_->board().my_deck.value().name);
  }
  return godot::String();
}

godot::String MtgcppSession::their_deck_name() const {
  if (session_ && session_->board().their_deck.has_value()) {
    return to_gd(session_->board().their_deck.value().name);
  }
  return godot::String();
}

bool MtgcppSession::can_start_table() const { return session_ && session_->canStartTable(); }

godot::String MtgcppSession::last_error() const {
  return session_ && session_->lastError().has_value() ? to_gd(session_->lastError().value())
                                                       : godot::String();
}

godot::Dictionary MtgcppSession::choose_deck(const godot::Ref<MtgcppDeck> &deck) {
  if (!session_) {
    return fail("not connected");
  }
  if (deck.is_null()) {
    return fail("choose_deck: null deck");
  }
  session_->chooseDeck(deck->to());
  return ok(godot::Dictionary{});
}

godot::Array MtgcppSession::hand(int64_t seat) const {
  if (!session_) {
    return godot::Array{};
  }
  const std::size_t index = static_cast<std::size_t>(seat);
  if (index >= session_->board().seats.size()) {
    return godot::Array{};
  }
  return board_cards_to_array(session_->board().seats[index].hand);
}

godot::Array MtgcppSession::zone_cards(int64_t seat, int64_t zone_index) const {
  if (!session_) {
    return godot::Array{};
  }
  const std::size_t si = static_cast<std::size_t>(seat);
  const std::size_t zi = static_cast<std::size_t>(zone_index);
  if (si >= session_->board().seats.size() || zi >= mtgcpp::core::kPlayerZoneCount) {
    return godot::Array{};
  }
  return board_cards_to_array(session_->board().seats[si].zones[zi]);
}

godot::Array MtgcppSession::stack_cards() const {
  return session_ ? board_cards_to_array(session_->board().stack) : godot::Array{};
}

int64_t MtgcppSession::life(int64_t seat) const {
  if (!session_) {
    return 0;
  }
  const std::size_t index = static_cast<std::size_t>(seat);
  if (index >= session_->board().life.size()) {
    return 0;
  }
  return session_->board().life[index];
}

void MtgcppSession::tap(int64_t seat, const godot::String &card_id) {
  if (session_) {
    session_->applyLocalAction(
        mtgcpp::state::tapCard(static_cast<mtgcpp::core::PlayerSeat>(seat), to_std(card_id)));
  }
}

void MtgcppSession::add_counter(int64_t seat, const godot::String &card_id) {
  if (session_) {
    session_->applyLocalAction(mtgcpp::state::addCounter(
        static_cast<mtgcpp::core::PlayerSeat>(seat), to_std(card_id)));
  }
}

void MtgcppSession::flip(int64_t seat, const godot::String &card_id) {
  if (session_) {
    session_->applyLocalAction(mtgcpp::state::flipCard(
        static_cast<mtgcpp::core::PlayerSeat>(seat), to_std(card_id)));
  }
}

void MtgcppSession::create_token(int64_t seat, int64_t zone_index, const godot::String &name) {
  if (session_) {
    session_->applyLocalAction(mtgcpp::state::createToken(
        static_cast<mtgcpp::core::PlayerSeat>(seat),
        static_cast<mtgcpp::core::PlayerZone>(zone_index), to_std(name)));
  }
}

void MtgcppSession::move_to_zone(int64_t seat, const godot::String &card_id, int64_t zone_index) {
  if (session_) {
    session_->applyLocalAction(mtgcpp::state::moveCardToZone(
        static_cast<mtgcpp::core::PlayerSeat>(seat), to_std(card_id),
        static_cast<mtgcpp::core::PlayerZone>(zone_index)));
  }
}

void MtgcppSession::move_to_stack(const godot::String &card_id) {
  if (session_) {
    session_->applyLocalAction(mtgcpp::state::moveCardToStack(to_std(card_id)));
  }
}

void MtgcppSession::set_life(int64_t seat, int64_t life) {
  if (session_) {
    session_->applyLocalAction(
        mtgcpp::state::setLife(static_cast<mtgcpp::core::PlayerSeat>(seat),
                               static_cast<int>(life)));
  }
}

void MtgcppSession::enter_sandbox() {
  if (session_) {
    session_->enterSandbox();
  }
}

bool MtgcppSession::is_sandbox() const { return session_ && session_->sandbox(); }

godot::Array MtgcppSession::library() const {
  if (!session_) {
    return godot::Array();
  }
  return board_cards_to_array(session_->library());
}

void MtgcppSession::draw_card() {
  if (session_) {
    session_->drawCard();
  }
}

void MtgcppSession::play_card(const godot::String &card_id) {
  if (session_) {
    session_->playCard(to_std(card_id));
  }
}

void MtgcppSession::place_from_library(const godot::String &card_id) {
  if (session_) {
    session_->placeFromLibrary(to_std(card_id));
  }
}

void MtgcppSession::request_hand_reveal() {
  if (session_) {
    session_->requestHandReveal();
  }
}

void MtgcppSession::accept_hand_reveal() {
  if (!session_ || !session_->role().has_value()) {
    return;
  }
  const std::size_t my_index =
      mtgcpp::state::seatIndex(session_->role().value());
  const std::vector<mtgcpp::core::BoardCard> &hand =
      session_->board().seats[my_index].hand;
  session_->acceptHandReveal(mtgcpp::core::toRevealCards(hand));
}

void MtgcppSession::deny_hand_reveal() {
  if (session_) {
    session_->denyHandReveal();
  }
}

void MtgcppSession::dismiss_reveal_prompt() {
  if (session_) {
    session_->dismissRevealPrompt();
  }
}

void MtgcppSession::clear_revealed() {
  if (session_) {
    session_->clearRevealed();
  }
}

godot::String MtgcppSession::reveal_request_from() const {
  if (session_ && session_->reveal().pending_request_from.has_value()) {
    return to_gd(session_->reveal().pending_request_from.value());
  }
  return godot::String();
}

bool MtgcppSession::reveal_accepted() const {
  return session_ && session_->reveal().reveal_accepted.has_value() &&
         session_->reveal().reveal_accepted.value();
}

godot::Array MtgcppSession::revealed_hand() const {
  godot::Array out;
  if (session_) {
    for (const mtgcpp::core::RevealCard &card : session_->reveal().revealed_hand) {
      godot::Dictionary d;
      d["id"] = to_gd(card.id);
      d["name"] = to_gd(card.name);
      d["image_url"] = to_gd(card.image_url);
      out.append(d);
    }
  }
  return out;
}

void MtgcppSession::_bind_methods() {
  ClassDB::bind_method(D_METHOD("create_room"), &MtgcppSession::create_room);
  ClassDB::bind_method(D_METHOD("join", "address"), &MtgcppSession::join);
  ClassDB::bind_method(D_METHOD("leave"), &MtgcppSession::leave);
  ClassDB::bind_method(D_METHOD("pump"), &MtgcppSession::pump);

  ClassDB::bind_method(D_METHOD("connected"), &MtgcppSession::connected);
  ClassDB::bind_method(D_METHOD("status"), &MtgcppSession::status);
  ClassDB::bind_method(D_METHOD("role"), &MtgcppSession::role);
  ClassDB::bind_method(D_METHOD("player_id"), &MtgcppSession::player_id);
  ClassDB::bind_method(D_METHOD("players"), &MtgcppSession::players);
  ClassDB::bind_method(D_METHOD("share_address"), &MtgcppSession::share_address);
  ClassDB::bind_method(D_METHOD("my_deck_name"), &MtgcppSession::my_deck_name);
  ClassDB::bind_method(D_METHOD("their_deck_name"), &MtgcppSession::their_deck_name);
  ClassDB::bind_method(D_METHOD("can_start_table"), &MtgcppSession::can_start_table);
  ClassDB::bind_method(D_METHOD("last_error"), &MtgcppSession::last_error);

  ClassDB::bind_method(D_METHOD("choose_deck", "deck"), &MtgcppSession::choose_deck);

  ClassDB::bind_method(D_METHOD("hand", "seat"), &MtgcppSession::hand);
  ClassDB::bind_method(D_METHOD("zone_cards", "seat", "zone_index"),
                       &MtgcppSession::zone_cards);
  ClassDB::bind_method(D_METHOD("stack_cards"), &MtgcppSession::stack_cards);
  ClassDB::bind_method(D_METHOD("life", "seat"), &MtgcppSession::life);

  ClassDB::bind_method(D_METHOD("tap", "seat", "card_id"), &MtgcppSession::tap);
  ClassDB::bind_method(D_METHOD("add_counter", "seat", "card_id"),
                       &MtgcppSession::add_counter);
  ClassDB::bind_method(D_METHOD("flip", "seat", "card_id"), &MtgcppSession::flip);
  ClassDB::bind_method(D_METHOD("create_token", "seat", "zone_index", "name"),
                       &MtgcppSession::create_token);
  ClassDB::bind_method(D_METHOD("move_to_zone", "seat", "card_id", "zone_index"),
                       &MtgcppSession::move_to_zone);
  ClassDB::bind_method(D_METHOD("move_to_stack", "card_id"), &MtgcppSession::move_to_stack);
  ClassDB::bind_method(D_METHOD("set_life", "seat", "life"), &MtgcppSession::set_life);

  ClassDB::bind_method(D_METHOD("enter_sandbox"), &MtgcppSession::enter_sandbox);
  ClassDB::bind_method(D_METHOD("is_sandbox"), &MtgcppSession::is_sandbox);
  ClassDB::bind_method(D_METHOD("library"), &MtgcppSession::library);
  ClassDB::bind_method(D_METHOD("draw_card"), &MtgcppSession::draw_card);
  ClassDB::bind_method(D_METHOD("play_card", "card_id"), &MtgcppSession::play_card);
  ClassDB::bind_method(D_METHOD("place_from_library", "card_id"),
                       &MtgcppSession::place_from_library);

  ClassDB::bind_method(D_METHOD("request_hand_reveal"), &MtgcppSession::request_hand_reveal);
  ClassDB::bind_method(D_METHOD("accept_hand_reveal"), &MtgcppSession::accept_hand_reveal);
  ClassDB::bind_method(D_METHOD("deny_hand_reveal"), &MtgcppSession::deny_hand_reveal);
  ClassDB::bind_method(D_METHOD("dismiss_reveal_prompt"), &MtgcppSession::dismiss_reveal_prompt);
  ClassDB::bind_method(D_METHOD("clear_revealed"), &MtgcppSession::clear_revealed);
  ClassDB::bind_method(D_METHOD("reveal_request_from"), &MtgcppSession::reveal_request_from);
  ClassDB::bind_method(D_METHOD("reveal_accepted"), &MtgcppSession::reveal_accepted);
  ClassDB::bind_method(D_METHOD("revealed_hand"), &MtgcppSession::revealed_hand);
}
