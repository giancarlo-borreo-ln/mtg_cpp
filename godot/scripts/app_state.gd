# App-wide shared state + services (autoloaded as `AppState`).
#
# Owns the card database and the deck repository (the two engine services the
# screens need), plus the persisted player selection and the transient deck
# editor state. Screens read/write through this singleton; they never do I/O or
# hold their own engine handles. All engine access happens on the main thread —
# the only exception is the card database's own async loader, which marshals
# back via call_deferred (single-owner rule).

extends Node

signal profile_changed(player_id: String)
signal db_loaded(ok: bool, loaded: int)
# Screen navigation: screens call goto("home"|"deck_editor"|"lobby"|"table");
# the app shell (app.gd) listens and swaps scenes.
signal navigate(screen: String)

# The app-data root. Everything persistent lives under `user://` (deck files,
# profile, art cache, card-db sidecar) so the port is relocatable.
const DATA_ROOT := "user://"

# Candidate card database locations, tried in order (bundled next to the app,
# then a local data/ dir). Missing files degrade to an empty DB — the editor
# surfaces a "database not loaded" hint rather than aborting.
const DB_CANDIDATES: Array[String] = [
	"res://data/default-cards.jsonl",
	"user://default-cards.jsonl",
]

var card_db: MtgcppCardDatabase
var deck_repo: MtgcppDeckRepository
var profile_id: String = ""

# Deck editor transient state (the deck being built).
var editing_deck_id: String = ""          # "" = a brand-new deck
var editor_cards: Array[MtgcppCard] = []  # rows of the deck being built
var editor_name: String = ""

var db_ready: bool = false
var db_loading: bool = false

# The active room session (null until Create Room / Join). The Lobby creates it;
# the Table reads it. Only the main thread touches it (pump in _process).
var session: MtgcppSession = null


func _ready() -> void:
	card_db = MtgcppCardDatabase.new()
	deck_repo = MtgcppDeckRepository.open(DATA_ROOT)
	profile_id = MtgcppProfileStore.load(DATA_ROOT)
	_load_db_async()


# --- room session (Lobby + Table share it) ----------------------------------

func start_host() -> Dictionary:
	if session == null:
		session = MtgcppSession.new()
	return session.create_room()


func start_guest(address: String) -> Dictionary:
	if session == null:
		session = MtgcppSession.new()
	return session.join(address)


func stop_session() -> void:
	if session != null:
		session.leave()
		session = null


# Begin a solo sandbox: host an embedded relay and load the bundled landfall
# deck as a drawable library (see Session's sandbox library). Returns the same
# { ok, error } shape as the room starters.
func start_sandbox() -> Dictionary:
	var result: Dictionary = start_host()
	if not bool(result.get("ok", false)):
		return result
	if session != null:
		session.enter_sandbox()
	# The card DB may still be loading (it can take a couple of minutes the
	# first time): apply the deck as soon as it is ready instead of silently
	# opening an empty library.
	if db_ready:
		_apply_sandbox_deck()
	elif not db_loaded.is_connected(_on_db_ready_for_sandbox):
		db_loaded.connect(_on_db_ready_for_sandbox, Object.CONNECT_ONE_SHOT)
	return {"ok": true}


func _on_db_ready_for_sandbox(_ok: bool, _loaded: int) -> void:
	_apply_sandbox_deck()


func _apply_sandbox_deck() -> void:
	if session == null:
		return
	var deck := _sandbox_deck()
	if deck != null:
		session.choose_deck(deck)
	session.enter_sandbox()


func _sandbox_deck() -> MtgcppDeck:
	const PATH := "res://assets/sandbox_landfall.txt"
	if not FileAccess.file_exists(PATH):
		return null
	var imported: Dictionary = card_db.import_deck(FileAccess.get_file_as_string(PATH))
	if not bool(imported.get("ok", false)):
		return null
	var preview: MtgcppImportPreview = imported["preview"]
	var cards: Array[MtgcppCard] = preview.resolved()
	if cards.is_empty():
		return null
	var deck := MtgcppDeck.new()
	deck.set_name("Landfall (Sandbox)")
	deck.set_format("Other")
	deck.set_cards(cards)
	return deck


# The deck vault as a list of summary dictionaries (for the Home list + Lobby).
func deck_summaries() -> Array:
	return deck_repo.list_summaries()


func select_profile(id: String) -> void:
	profile_id = id
	MtgcppProfileStore.save(DATA_ROOT, id)
	profile_changed.emit(id)


func switch_profile() -> void:
	MtgcppProfileStore.clear(DATA_ROOT)
	profile_id = ""
	profile_changed.emit("")


# Begin building a brand-new deck (clears any in-progress editor state).
func begin_new_deck() -> void:
	editing_deck_id = ""
	editor_cards = []
	editor_name = ""


func goto(screen: String) -> void:
	navigate.emit(screen)


# Load an existing deck into the editor for editing.
func begin_edit_deck(id: String) -> void:
	editing_deck_id = id
	editor_cards = []
	editor_name = ""
	var read: Dictionary = deck_repo.read(id)
	if bool(read.get("ok", false)):
		var deck: MtgcppDeck = read["deck"]
		editor_name = deck.get_name()
		editor_cards = deck.get_cards()


# --- card database (async, non-blocking) ------------------------------------

func _load_db_async() -> void:
	if db_loading:
		return
	db_loading = true
	db_ready = false
	card_db.load_finished.connect(_on_db_loaded)
	# Try each candidate; load_from_file_async on a missing file resolves to
	# ok=false (empty DB), which degrades gracefully.
	var jsonl := _first_existing(_db_candidates())
	if jsonl.is_empty():
		db_loading = false
		db_ready = true
		db_loaded.emit(false, 0)
		return
	card_db.load_from_file_async(jsonl, _db_cache_path())


# Where the DB can live. Besides the bundled copies, the desktop port reuses
# the dev checkout's `data/` dir (the repo root sits next to `godot/`) and the
# executable's own `data/` dir, so a source run and a packaged build both find
# the cards. An explicit `MTG_CPP_CARD_DB` env var wins.
func _db_candidates() -> Array[String]:
	var out: Array[String] = []
	var override := OS.get_environment("MTG_CPP_CARD_DB")
	if not override.is_empty():
		out.append(override)
	out.append_array(DB_CANDIDATES)
	if DisplayServer.get_name() != "headless":
		# Headless (tests/CI) stays fast and deterministic on the bundled copies.
		out.append(ProjectSettings.globalize_path("res://")
			.path_join("../data/default-cards.jsonl"))
	out.append(OS.get_executable_path().get_base_dir()
		.path_join("data/default-cards.jsonl"))
	return out


# The card-db sidecar. Reuse the desktop app's cache when present (same file,
# same key, so it loads instantly); otherwise build one under `user://`.
func _db_cache_path() -> String:
	var shared := MtgcppDataPaths.default_app().card_db_cache()
	if FileAccess.file_exists(shared):
		return shared
	return DATA_ROOT + "card_db.cache"


func _first_existing(paths: Array) -> String:
	for p in paths:
		if FileAccess.file_exists(p):
			return p
	return ""


func _on_db_loaded(ok: bool, _err: String, loaded: int, _rejected: int) -> void:
	db_loading = false
	db_ready = true
	db_loaded.emit(ok, loaded)
