# Headless UI smoke test (Phase 3): the app shell + Home screen render and
# navigate. Runs the same autoloads as the real app, then drives the Home screen
# through its states. UI/UX checks: picker → vault transition, and the themed
# controls are present.

extends Node

var _failures: Array[String] = []


func _check(condition: bool, label: String) -> void:
	if condition:
		print("PASS ", label)
	else:
		_failures.append(label)
		print("FAIL ", label)


func _ready() -> void:
	_check(AppState.card_db != null, "ui.appstate.db")
	_check(AppState.deck_repo != null, "ui.appstate.repo")
	_check(Palette.BACKGROUND is Color, "ui.palette")

	# Reset any persisted selection so the picker (not the vault) is the starting
	# state, making the test deterministic regardless of a previous run's profile.
	AppState.switch_profile()

	var home: Control = load("res://scenes/home.tscn").instantiate()
	add_child(home)
	await _frame()
	_check(_has_label(home, "Choose your player"), "ui.home.picker")

	AppState.select_profile("carlo")
	await _frame()
	_check(_has_label(home, "Deck Vault — Carlo"), "ui.home.vault")
	_check(not _has_label(home, "Choose your player"), "ui.home.picker.gone")

	# The vault shows the action buttons even with zero decks.
	_check(_has_button(home, "Play Online"), "ui.home.play_online")
	_check(_has_button(home, "+ New Deck"), "ui.home.new_deck")

	# Navigating to the deck editor (via AppState) and back must not crash.
	AppState.begin_new_deck()
	AppState.goto("deck_editor")
	await _frame()
	_check(AppState.editing_deck_id == "", "ui.editor.new")

	# Lobby and Table scenes instantiate without error (connect panel / empty
	# state, since there is no active session in this headless test).
	var lobby: Control = load("res://scenes/lobby.tscn").instantiate()
	add_child(lobby)
	await _frame()
	_check(_has_button(lobby, "Create Room"), "ui.lobby.connect")

	var table: Control = load("res://scenes/table.tscn").instantiate()
	add_child(table)
	await _frame()
	_check(_has_button(table, "Leave"), "ui.table.toolbar")

	await _check_art_cache()

	if _failures.is_empty():
		print("UI SMOKE OK")
		get_tree().quit(0)
	else:
		print("UI SMOKE FAIL: ", _failures.size(), " failure(s)")
		get_tree().quit(1)


# Art cache: the injection seam resolves art, a miss is offline-safe (no
# network when disabled), and a CardView swaps to the real image when present.
func _check_art_cache() -> void:
	ArtCache.set_enabled(false)
	_check(ArtCache.texture_for("missing", "https://example.invalid/a.png") == null,
		"ui.artcache.offline")

	ArtCache.set_enabled(true)
	var image := Image.create(4, 4, false, Image.FORMAT_RGBA8)
	image.fill(Palette.ACCENT)
	var texture := ImageTexture.create_from_image(image)
	ArtCache.put_texture("unit-key", texture)
	_check(ArtCache.texture_for("unit-key", "") == texture, "ui.artcache.inject")
	_check(ArtCache.cached_count() >= 1, "ui.artcache.count")

	var view = load("res://scripts/widgets/card_view.gd").new()
	view.configure({"id": "x", "scryfall_id": "unit-key", "image_url": "https://x"},
		0, 0, false, false)
	add_child(view)
	await _frame()
	_check(view._art_shown, "ui.cardview.art")
	view.queue_free()


func _frame() -> void:
	await get_tree().process_frame
	await get_tree().process_frame


func _has_label(node: Node, text: String) -> bool:
	if node is Label and node.text == text:
		return true
	for child in node.get_children():
		if _has_label(child, text):
			return true
	return false


func _has_button(node: Node, text: String) -> bool:
	if node is Button and node.text == text:
		return true
	for child in node.get_children():
		if _has_button(child, text):
			return true
	return false
