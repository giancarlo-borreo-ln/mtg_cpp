# Table UI smoke test (Phase 3 UX): drives the modern battlefield against a real
# (loopback host) session. Verifies card faces render, tap animates, drag & drop
# moves a card between zones through `_can_drop_data`/`_drop_data`, and the
# layout survives a window resize.

extends Node

const CardViewScript := preload("res://scripts/widgets/card_view.gd")

var _failures: Array[String] = []
var _session: MtgcppSession
var _table: Control
var _seat := 0


func _check(condition: bool, label: String) -> void:
	if condition:
		print("PASS ", label)
	else:
		_failures.append(label)
		print("FAIL ", label)


func _ready() -> void:
	await _run()
	if _failures.is_empty():
		print("TABLE SMOKE OK")
		get_tree().quit(0)
	else:
		print("TABLE SMOKE FAIL: ", _failures.size(), " failure(s)")
		get_tree().quit(1)


func _run() -> void:
	_session = MtgcppSession.new()
	var room: Dictionary = _session.create_room()
	_check(bool(room.get("ok", false)), "table.host.create")
	_check(await _wait_until(func() -> bool: return _session.role() >= 0), "table.role")

	_seat = _session.role()
	AppState.session = _session

	# Two creatures in my creatures zone (index 1) to render + drag.
	_session.create_token(_seat, 1, "Goblin")
	_session.create_token(_seat, 1, "Elf")

	_table = load("res://scenes/table.tscn").instantiate()
	add_child(_table)
	await _frames(4)

	_check(_find_meta(_table, "zone_kind").size() >= 11, "table.zone_panels")
	var faces := _card_faces()
	_check(faces.size() >= 2, "table.card_faces")

	# --- selection ----------------------------------------------------------
	var first: Dictionary = _session.zone_cards(_seat, 1)[0]
	var view := _card_view(first["id"])
	_check(view != null, "table.card_view.present")
	if view != null:
		view.activated.emit(view.card, _seat, 1)
		await _frames(1)
		_check(str(_table._selected.get("id", "")) == str(first["id"]), "table.select")
		_check(view.get_theme_stylebox("panel").border_color == Palette.ACCENT, "table.select.highlight")

	# --- verb toolbar (tooltip + wired action + retained selection) ---------
	var tap_button := _find_button(_table, "Tap")
	_check(tap_button != null and tap_button.tooltip_text != "", "table.verb.tap")
	var second: Dictionary = _session.zone_cards(_seat, 1)[1]
	var second_view := _card_view(second["id"])
	if tap_button != null and second_view != null:
		second_view.activated.emit(second_view.card, _seat, 1)
		await _frames(1)
		tap_button.emit_signal("pressed")
		await _frames(4)
		_check(_is_tapped(second["id"]), "table.verb.tap.applied")
		_check(str(_table._selected.get("id", "")) == str(second["id"]),
			"table.verb.selection_retained")

	# --- Move verb opens the zone menu and applies the choice ---------------
	var move_button := _find_button(_table, "Move")
	if move_button != null:
		move_button.emit_signal("pressed")
		await _frames(1)
		var menu := _find_popup(_table)
		_check(menu != null, "table.verb.move.menu")
		if menu != null:
			menu.id_pressed.emit(3)
			await _frames(4)
			_check(_zone_count(_seat, 3) == 1, "table.verb.move.applied")

	# --- tap animates -------------------------------------------------------
	_session.tap(_seat, first["id"])
	var tapped := false
	for _i in range(60):
		await get_tree().process_frame
		var v := _card_view(first["id"])
		if v != null and abs(v.rotation - PI / 2.0) < 0.05:
			tapped = true
			break
	_check(tapped, "table.tap.animate")

	# --- drag & drop between zones -----------------------------------------
	var creatures_before: int = _session.zone_cards(_seat, 1).size()
	var target_panel: Control = _zone_panel(_seat, 0)
	_check(target_panel != null, "table.drop_target")
	if target_panel != null:
		var data := {"type": CardViewScript.DRAG_TYPE, "card": first, "seat": _seat, "zone": 1}
		var local := target_panel.get_global_rect().get_center() - _table.get_global_rect().position
		_check(_table._can_drop_data(local, data), "table.drop.allowed")
		_table._drop_data(local, data)
		await _frames(4)
		_check(_session.zone_cards(_seat, 0).size() == 1, "table.drop.moved")
		_check(_session.zone_cards(_seat, 1).size() == creatures_before - 1, "table.drop.source")

	# --- resize relayout ----------------------------------------------------
	var before := _table.size
	get_viewport().size = Vector2i(900, 600)
	await _frames(3)
	_check(_table.size != before, "table.resize.relayout")
	_check(_card_faces().size() >= 2, "table.resize.cards")
	get_viewport().size = Vector2i(1280, 720)
	await _frames(2)

	_session.leave()
	AppState.session = null

	await _run_sandbox()


# Sandbox flow: library panel, infinite draw and type-routed placement.
func _run_sandbox() -> void:
	var sandbox := MtgcppSession.new()
	AppState.session = sandbox
	var room: Dictionary = sandbox.create_room()
	_check(bool(room.get("ok", false)), "sandbox.host.create")

	var db := MtgcppCardDatabase.new()
	db.load_from_file("res://tests/fixtures/tiny_cards.jsonl", "user://sandbox.cache")
	var cards: Array[MtgcppCard] = []
	for c in db.search("lightning bolt", 5):
		cards.append(c)
	for c in db.search("island", 5):
		cards.append(c)
	var deck := MtgcppDeck.new()
	deck.set_name("Sandbox Test")
	deck.set_format("Other")
	deck.set_cards(cards)

	var ready := false
	for _i in range(400):
		sandbox.pump()
		if sandbox.role() >= 0:
			ready = true
			break
		await get_tree().process_frame
	_check(ready, "sandbox.role")
	sandbox.choose_deck(deck)
	sandbox.enter_sandbox()
	sandbox.pump()
	_check(sandbox.is_sandbox(), "sandbox.enter")
	_check(sandbox.library().size() == 2, "sandbox.library.size")

	_table = load("res://scenes/table.tscn").instantiate()
	add_child(_table)
	await _frames(4)

	_check(_find_button(_table, "Draw") != null, "sandbox.draw.button")
	_check(_find_button(_table, "Lightning Bolt") != null, "sandbox.library.row")

	# Draw one card; the library is shuffled, so assert by type rather than by a
	# fixed order (Island -> Lands, Lightning Bolt -> Stack).
	sandbox.draw_card()
	await _frames(3)
	_check(sandbox.hand(_seat).size() == 1, "sandbox.draw.hand")
	_check(sandbox.library().size() == 1, "sandbox.draw.library")

	var remaining: Dictionary = sandbox.library()[0]
	var remaining_name := str(remaining["name"])
	var row := _find_button(_table, remaining_name)
	_check(row != null, "sandbox.library.row.remaining")
	if row != null:
		row.emit_signal("pressed")
		await _frames(4)
		_check(sandbox.library().size() == 0, "sandbox.place.removed")
		if remaining_name == "Island":
			_check(sandbox.zone_cards(_seat, 0).size() == 1, "sandbox.place.lands")
			_check(sandbox.stack_cards().size() == 0, "sandbox.place.notstack")
		else:
			_check(sandbox.stack_cards().size() == 1, "sandbox.place.stack")
			_check(sandbox.zone_cards(_seat, 0).size() == 0, "sandbox.place.notlands")

	# Play the hand card: it routes to the complementary pile.
	var hand: Array = sandbox.hand(_seat)
	_check(hand.size() == 1, "sandbox.play.hand")
	if hand.size() == 1:
		var name := str(hand[0]["name"])
		sandbox.play_card(str(hand[0]["id"]))
		await _frames(4)
		if name == "Island":
			_check(sandbox.zone_cards(_seat, 0).size() == 1, "sandbox.play.lands")
		else:
			_check(sandbox.stack_cards().size() == 1, "sandbox.play.stack")
		_check(sandbox.hand(_seat).size() == 0, "sandbox.play.emptied")

	sandbox.leave()
	AppState.session = null

	# The Lobby path (auto-loading the bundled deck) must never crash, even
	# with an empty card database (it just opens an empty library).
	var started: Dictionary = AppState.start_sandbox()
	_check(bool(started.get("ok", false)), "sandbox.appstate.start")
	_check(AppState.session != null, "sandbox.appstate.session")
	AppState.stop_session()


# --- helpers ----------------------------------------------------------------

func _frames(count: int) -> void:
	for _i in range(count):
		await get_tree().process_frame


func _card_faces() -> Array:
	return _find_meta(_table, "card_id")


func _card_view(card_id: String) -> Control:
	for node in _card_faces():
		if str(node.get_meta("card_id", "")) == str(card_id):
			return node as Control
	return null


func _zone_count(seat: int, zone: int) -> int:
	return _session.zone_cards(seat, zone).size()


func _find_popup(node: Node) -> PopupMenu:
	if node is PopupMenu:
		return node as PopupMenu
	for child in node.get_children():
		var found := _find_popup(child)
		if found != null:
			return found
	return null


func _is_tapped(card_id: String) -> bool:
	for card: Dictionary in _session.zone_cards(_seat, 1):
		if str(card["id"]) == str(card_id):
			return bool(card["tapped"])
	return false


func _find_button(node: Node, text: String) -> Button:
	if node is Button and node.text == text:
		return node as Button
	for child in node.get_children():
		var found := _find_button(child, text)
		if found != null:
			return found
	return null


func _zone_panel(seat: int, zone: int) -> Control:
	for node in _find_meta(_table, "zone_kind"):
		if str(node.get_meta("zone_kind", "")) != "zone":
			continue
		if int(node.get_meta("zone_seat", -1)) == seat \
				and int(node.get_meta("zone_index", -1)) == zone:
			return node as Control
	return null


func _find_meta(node: Node, key: String) -> Array:
	var out: Array = []
	if node.has_meta(key):
		out.append(node)
	for child in node.get_children():
		out.append_array(_find_meta(child, key))
	return out


func _wait_until(predicate: Callable) -> bool:
	for _i in range(400):
		_session.pump()
		if predicate.call():
			return true
		await get_tree().process_frame
	return false
