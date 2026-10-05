# Smoke test for the mtg_cpp GDExtension (Phase 2, gate 2.9).
#
# Runs headless: exercises the public bindings and quits with exit code 0 on
# success or 1 on the first failure. `--headless` + `--quit` drive it from CI
# without a display. UI/UX note: this script is a *test*, not a screen — the
# real UI arrives in Phase 3 (scenes + widgets), where hover/focus/contrast and
# responsive layout rules apply.

extends Node

var _failures: Array[String] = []


func _check(condition: bool, label: String) -> void:
	if condition:
		print("PASS ", label)
	else:
		_failures.append(label)
		print("FAIL ", label)


func _ready() -> void:
	_test_data_paths()
	_test_card_database()
	_test_deck_repository()
	_test_board_state()
	_test_editor_and_import()
	await _test_async_load()

	if _failures.is_empty():
		print("SMOKE OK")
		get_tree().quit(0)
	else:
		print("SMOKE FAIL: ", _failures.size(), " failure(s)")
		get_tree().quit(1)


func _test_data_paths() -> void:
	var paths := MtgcppDataPaths.from_root("/tmp/mtg-cpp/godot-smoke")
	_check(paths.get_root() == "/tmp/mtg-cpp/godot-smoke", "datapaths.root")
	_check(paths.card_db_cache() == "/tmp/mtg-cpp/godot-smoke/card_db.cache", "datapaths.cache")
	_check(paths.decks_dir() == "/tmp/mtg-cpp/godot-smoke/decks", "datapaths.decks")
	_check(paths.profile_file() == "/tmp/mtg-cpp/godot-smoke/profile.json", "datapaths.profile")
	_check(paths.art_cache_dir() == "/tmp/mtg-cpp/godot-smoke/art_cache", "datapaths.art")


func _test_card_database() -> void:
	var db := MtgcppCardDatabase.new()
	_check(db.is_empty(), "db.is_empty")
	_check(db.size() == 0, "db.size")
	_check(db.search("lightning bolt", 10).is_empty(), "db.search.empty")


func _test_deck_repository() -> void:
	var repo := MtgcppDeckRepository.open("/tmp/mtg-cpp/godot-smoke/repo")
	var deck := MtgcppDeck.new()
	deck.name = "Smoke Deck"
	deck.format = "Standard"

	var created: Dictionary = repo.create(deck)
	_check(bool(created.get("ok", false)), "repo.create.ok")
	var stored := created.get("deck") as MtgcppDeck
	_check(stored != null and stored.get_id() != "", "repo.create.id")
	_check(stored.get_name() == "Smoke Deck", "repo.create.name")

	var read: Dictionary = repo.read(stored.get_id())
	_check(bool(read.get("ok", false)), "repo.read.ok")
	var read_back := read.get("deck") as MtgcppDeck
	_check(read_back != null and read_back.get_name() == "Smoke Deck", "repo.read.name")

	_check(repo.list().size() == 1, "repo.list.size")

	var missing: Dictionary = repo.read("no-such-id")
	_check(not bool(missing.get("ok", true)), "repo.read.missing.fails")

	_check(repo.remove(stored.get_id()), "repo.remove")
	_check(repo.list().is_empty(), "repo.list.after_remove")


func _test_board_state() -> void:
	var board := MtgcppBoardState.initial()
	_check(board.life(0) == 20, "board.life.0")
	_check(board.life(1) == 20, "board.life.1")
	_check(board.life(2) == 0, "board.life.bounds")


# The async load must not block the main thread: the worker parses off-thread
# and the `load_finished` signal lands on a later frame. We await it (bounded)
# and then assert the DB is searchable.
func _test_async_load() -> void:
	var db := MtgcppCardDatabase.new()
	var done: Array = [false]
	var result: Dictionary = {}
	db.load_finished.connect(func(ok: bool, err: String, loaded: int, rejected: int) -> void:
		done[0] = true
		# Mutate the shared Dictionary (reference type); reassigning `result`
		# would only rebind the lambda's captured copy, never the outer one.
		result["ok"] = ok
		result["error"] = err
		result["loaded"] = loaded
		result["rejected"] = rejected
	)

	db.load_from_file_async("res://tests/fixtures/tiny_cards.jsonl", "user://tiny_cards.cache")

	for _i in range(200):
		if done[0]:
			break
		await get_tree().process_frame

	_check(done[0], "db.async.finished")
	_check(bool(result.get("ok", false)), "db.async.ok")
	_check(int(result.get("loaded", 0)) == 2, "db.async.loaded")
	_check(int(result.get("rejected", 0)) == 0, "db.async.rejected")
	_check(db.size() == 2, "db.async.size")
	_check(db.search("lightning bolt", 10).size() == 1, "db.async.search")


# Profiles, deck-editor pure ops, Arena export, and the guarded import preview
# all run synchronously against the tiny fixture DB.
func _test_editor_and_import() -> void:
	var db := MtgcppCardDatabase.new()
	var load: Dictionary = db.load_from_file("res://tests/fixtures/tiny_cards.jsonl", "user://tiny2.cache")
	_check(bool(load.get("ok", false)), "editor.load")

	var profiles: Array = MtgcppProfiles.list()
	_check(profiles.size() == 4, "profiles.size")
	_check(profiles[0]["id"] == "carlo", "profiles.first")

	var results: Array = db.search("lightning bolt", 10)
	_check(results.size() == 1, "editor.search")
	var card: MtgcppCard = results[0]

	var deck: Array[MtgcppCard] = []
	deck = MtgcppDeckEditor.add_card(deck, card)
	_check(deck.size() == 1, "editor.add")
	_check(deck[0].get_quantity() == 1, "editor.qty1")
	deck = MtgcppDeckEditor.set_quantity(deck, card.key(), 3)
	_check(deck[0].get_quantity() == 3, "editor.qty3")
	var totals: Dictionary = MtgcppDeckEditor.totals(deck)
	_check(int(totals["total"]) == 3, "editor.total")
	_check(int(totals["unique"]) == 1, "editor.unique")

	var text: String = MtgcppArena.to_text(deck)
	_check(text.contains("Lightning Bolt"), "arena.export")

	var imp: Dictionary = db.import_deck("1 Lightning Bolt (lb) 1\n")
	_check(bool(imp.get("ok", false)), "import.ok")
	var preview: MtgcppImportPreview = imp["preview"]
	_check(preview.can_confirm(), "import.can_confirm")
	_check(preview.confirm().size() == 1, "import.confirm")

	var imp2: Dictionary = db.import_deck("1 Not A Real Card\n")
	_check(bool(imp2.get("ok", false)), "import2.ok")
	var preview2: MtgcppImportPreview = imp2["preview"]
	_check(not preview2.can_confirm(), "import2.cannot_confirm")
	_check(preview2.missing().size() == 1, "import2.missing")
