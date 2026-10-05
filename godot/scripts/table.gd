# Table screen (Phase 3.5) — the battlefield.
#
# Layout: for each half, creatures on top, lands and artifacts along the bottom
# (artifacts to the side below the lands), graveyard over exile in a right-hand
# column, and — in the sandbox — the library beside them. The hand is a flat row
# of card rectangles (never a fanned "held" hand).
#
# Cards are procedural faces (`widgets/card_view.gd`): click selects, the verb
# toolbar (or T/C/K/F/S/M) acts, and own cards drag between piles. In the sandbox
# a Draw button fills the hand from the library and clicking a library card puts
# it straight into play, auto-routed by type (lands/creatures/artifacts to their
# pile, instants/sorceries/enchantments onto the Stack).
#
# Everything reads/writes through `AppState.session` on the main thread; the
# session's pump runs once per frame in `_process` (single-owner rule).

extends Control

const CardViewScript := preload("res://scripts/widgets/card_view.gd")

const Z_LANDS := 0
const Z_CREATURES := 1
const Z_SPELLS := 2
const Z_GRAVEYARD := 3
const Z_EXILE := 4
const Z_ARTIFACTS := 5

const ZONE_NAMES := ["Lands", "Creatures", "Instants/Sorceries", "Graveyard", "Exile",
	"Artifacts"]

# Card face sizes: the hand is largest/legible, own piles mid, opponent/stack
# smallest.
const CARD_HAND := 1.0
const CARD_ZONE := 0.92
const CARD_SMALL := 0.66

@onready var toolbar: HBoxContainer = $Margin/VBox/Toolbar
@onready var status_bar: HBoxContainer = $Margin/VBox/Status
@onready var board: VBoxContainer = $Margin/VBox/Board
@onready var hand_scroll: ScrollContainer = $Margin/VBox/Hand
@onready var hand_row: HBoxContainer = $Margin/VBox/Hand/HandRow
@onready var hint: Label = $Margin/VBox/Hint

var _selected := {}              # { id, seat, zone, name } or {} when nothing
var _signature := ""             # coarse board signature to avoid rebuild-per-frame
var _verb_buttons: Array[Button] = []
var _card_views: Array = []
var _zone_nodes: Array = []      # { node, seat, zone, kind }
var _pending_anim := {}          # { card_id, from, card }
var _prev_tapped := {}           # card id -> tapped, to animate state flips


func _ready() -> void:
	_build_toolbar()
	_update_verbs()
	_update_hint()


func _process(_delta: float) -> void:
	var session: MtgcppSession = AppState.session
	if session != null:
		session.pump()

	var sig := _signature_of(session)
	if sig != _signature:
		_signature = sig
		_render(session)
		if not _pending_anim.is_empty():
			call_deferred("_play_pending_anim")


# --- session helpers --------------------------------------------------------

func _my_seat(session: MtgcppSession) -> int:
	if session == null:
		return 0
	return int(session.role())


func _signature_of(session: MtgcppSession) -> String:
	var parts := PackedStringArray()
	if session == null:
		parts.append("no-session")
		return "|".join(parts)
	parts.append(str(_my_seat(session)))
	parts.append(str(session.life(0)))
	parts.append(str(session.life(1)))
	parts.append(session.my_deck_name())
	parts.append(session.their_deck_name())
	parts.append(session.reveal_request_from())
	parts.append("sandbox" if session.is_sandbox() else "online")
	for seat in [0, 1]:
		parts.append(_cards_sig(session.hand(seat)))
		for z in range(ZONE_NAMES.size()):
			parts.append(_cards_sig(session.zone_cards(seat, z)))
	parts.append(_cards_sig(session.stack_cards()))
	parts.append(_cards_sig(session.library()))
	return "|".join(parts)


func _cards_sig(cards: Array) -> String:
	var ids := PackedStringArray()
	for card: Dictionary in cards:
		ids.append("%s:%d:%d:%d:%d" % [
			card.get("id", ""), int(card.get("counters", 0)),
			int(bool(card.get("tapped", false))), int(bool(card.get("flipped", false))),
			int(bool(card.get("is_token", false))),
		])
	return ",".join(ids)


# --- toolbar + status -------------------------------------------------------

func _build_toolbar() -> void:
	for child in toolbar.get_children():
		toolbar.remove_child(child)
		child.queue_free()
	_verb_buttons.clear()

	var leave := UI.button("Leave", false, true)
	leave.tooltip_text = "Leave the table and return to the lobby"
	leave.pressed.connect(_leave)
	toolbar.add_child(leave)

	var draw := UI.button("Draw", true)
	draw.tooltip_text = "Draw a card from the library (sandbox)"
	draw.pressed.connect(func() -> void:
		if AppState.session != null:
			AppState.session.draw_card())
	toolbar.add_child(draw)

	var spacer := Control.new()
	spacer.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	toolbar.add_child(spacer)

	_add_verb("Play", "Play the selected hand card where it belongs (P)",
		func(s: Dictionary) -> void: AppState.session.play_card(s["id"]))
	_add_verb("Tap", "Tap or untap the selected card (T)",
		func(s: Dictionary) -> void: AppState.session.tap(s["seat"], s["id"]))
	_add_verb("+1/+1", "Add a +1/+1 counter (C)",
		func(s: Dictionary) -> void: AppState.session.add_counter(s["seat"], s["id"]))
	_add_verb("Flip", "Flip the card to its other side (F)",
		func(s: Dictionary) -> void: AppState.session.flip(s["seat"], s["id"]))
	_add_verb("Token", "Create a token in your creatures zone (K)",
		func(s: Dictionary) -> void: AppState.session.create_token(s["seat"], Z_CREATURES, "Token"))
	_add_verb("Stack", "Move the selected card onto the stack (S)",
		func(s: Dictionary) -> void: AppState.session.move_to_stack(s["id"]))
	_add_verb("Move", "Move the selected card to a zone (M)", _open_move_menu)

	var reveal := UI.button("Request Hand Reveal")
	reveal.tooltip_text = "Ask your opponent to reveal their hand"
	reveal.pressed.connect(func() -> void:
		if AppState.session != null:
			AppState.session.request_hand_reveal())
	toolbar.add_child(reveal)


func _add_verb(label: String, tooltip: String, action: Callable) -> void:
	var b := UI.button(label)
	b.tooltip_text = tooltip
	b.custom_minimum_size.x = 68
	b.pressed.connect(func() -> void:
		if _selected.is_empty() or AppState.session == null:
			return
		# Keep the selection: actions re-render the board and the highlight
		# follows the card. `Move` needs it until its zone popup resolves.
		action.call(_selected)
		_update_hint())
	_verb_buttons.append(b)
	toolbar.add_child(b)


func _update_verbs() -> void:
	var enabled := not _selected.is_empty() and AppState.session != null
	for b in _verb_buttons:
		b.disabled = not enabled


func _leave() -> void:
	AppState.stop_session()
	_selected = {}
	AppState.goto("lobby")


# --- rendering --------------------------------------------------------------

func _render(session: MtgcppSession) -> void:
	_card_views.clear()
	_zone_nodes.clear()
	for child in board.get_children():
		board.remove_child(child)
		child.queue_free()
	for child in hand_row.get_children():
		hand_row.remove_child(child)
		child.queue_free()

	if session == null:
		board.add_child(UI.label(
			"No active session — use the Lobby to create or join a room.",
			16, Palette.TEXT_MUTED))
		_render_status(null)
		return

	_render_status(session)

	var my_seat := _my_seat(session)
	var their_seat := 1 - my_seat

	board.add_child(_half(session, their_seat, false))
	board.add_child(_stack_panel(session))
	board.add_child(_half(session, my_seat, true))

	for card: Dictionary in session.hand(my_seat):
		hand_row.add_child(_card_view(card, my_seat, -1, true, CARD_HAND))

	_render_reveal(session)
	_animate_tap_flips()
	_update_hint()


func _render_status(session: MtgcppSession) -> void:
	for child in status_bar.get_children():
		status_bar.remove_child(child)
		child.queue_free()
	if session == null:
		return

	var my_seat := _my_seat(session)
	var their_seat := 1 - my_seat

	var their_name := session.their_deck_name()
	if their_name.is_empty():
		their_name = "Opponent"
	status_bar.add_child(UI.label(their_name, 16, Palette.TEXT_MUTED))
	status_bar.add_child(_life_button(session, their_seat, false))

	var spacer := Control.new()
	spacer.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	status_bar.add_child(spacer)

	var hand_n := session.hand(my_seat).size()
	var sandbox := session.is_sandbox()
	var lib := session.library().size() if sandbox else 0
	status_bar.add_child(_life_button(session, my_seat, true))
	status_bar.add_child(UI.label("hand: %d" % hand_n, 14, Palette.TEXT_MUTED))
	if sandbox:
		status_bar.add_child(UI.label("· library: %d" % lib, 14, Palette.TEXT_MUTED))


func _life_button(session: MtgcppSession, seat: int, editable: bool) -> Button:
	var b := UI.button("♥ %d" % session.life(seat), false, false)
	b.custom_minimum_size.x = 84
	b.tooltip_text = "Your life total — click to edit" if editable else "Opponent life total"
	if editable:
		b.pressed.connect(func() -> void: _edit_life(seat, session.life(seat)))
	else:
		b.disabled = true
	return b


# My battlefield half: creatures on top; lands + artifacts on the bottom row;
# graveyard/exile in a right column; the sandbox library beside them. The
# opponent's half is a single compact row of the same piles so both fit on
# screen.
func _half(session: MtgcppSession, seat: int, mine: bool) -> Control:
	if not mine:
		var row := HBoxContainer.new()
		row.add_theme_constant_override("separation", 6)
		for z in [Z_CREATURES, Z_LANDS, Z_ARTIFACTS, Z_GRAVEYARD, Z_EXILE]:
			row.add_child(_zone_panel(session, seat, z, false, false, 124))
		return row

	var half := HBoxContainer.new()
	half.add_theme_constant_override("separation", 8)
	half.size_flags_vertical = Control.SIZE_EXPAND_FILL

	var main := VBoxContainer.new()
	main.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	main.add_theme_constant_override("separation", 6)
	main.add_child(_zone_panel(session, seat, Z_CREATURES, true, true, 176))

	var bottom := HBoxContainer.new()
	bottom.add_theme_constant_override("separation", 6)
	bottom.add_child(_zone_panel(session, seat, Z_LANDS, true, true, 176))
	bottom.add_child(_zone_panel(session, seat, Z_ARTIFACTS, true, false, 176))
	main.add_child(bottom)
	half.add_child(main)

	var side := VBoxContainer.new()
	side.custom_minimum_size.x = 168
	side.add_theme_constant_override("separation", 6)
	side.add_child(_zone_panel(session, seat, Z_GRAVEYARD, true, false, 176))
	side.add_child(_zone_panel(session, seat, Z_EXILE, true, false, 176))
	half.add_child(side)

	if session.is_sandbox():
		half.add_child(_library_panel(session))
	return half


func _zone_panel(session: MtgcppSession, seat: int, zone: int, mine: bool,
		expand: bool, min_h: int = 100) -> PanelContainer:
	var panel := PanelContainer.new()
	panel.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	panel.size_flags_vertical = Control.SIZE_EXPAND_FILL if expand else Control.SIZE_SHRINK_BEGIN
	panel.custom_minimum_size = Vector2(120, min_h)
	var sb := StyleBoxFlat.new()
	sb.bg_color = Palette.SURFACE
	sb.set_corner_radius_all(Palette.RADIUS)
	sb.border_color = Palette.BORDER
	sb.set_border_width_all(1)
	sb.content_margin_left = 6
	sb.content_margin_right = 6
	sb.content_margin_top = 4
	sb.content_margin_bottom = 4
	panel.add_theme_stylebox_override("panel", sb)
	panel.set_meta("zone_seat", seat)
	panel.set_meta("zone_index", zone)
	panel.set_meta("zone_kind", "zone")
	_zone_nodes.append({"node": panel, "seat": seat, "zone": zone, "kind": "zone"})

	var v := VBoxContainer.new()
	v.add_theme_constant_override("separation", 3)

	var cards: Array = session.zone_cards(seat, zone)
	var header := HBoxContainer.new()
	var title := UI.label(ZONE_NAMES[zone].to_upper(), 10, Palette.TEXT_FAINT)
	title.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	title.clip_text = true
	header.add_child(title)
	header.add_child(UI.label(str(cards.size()), 10, Palette.TEXT_FAINT))
	v.add_child(header)

	var flow := HFlowContainer.new()
	flow.add_theme_constant_override("h_separation", 3)
	flow.add_theme_constant_override("v_separation", 3)
	if cards.is_empty():
		flow.add_child(UI.label("—", 11, Palette.TEXT_FAINT))
	for card: Dictionary in cards:
		flow.add_child(_card_view(card, seat, zone, mine, CARD_ZONE if mine else CARD_SMALL))
	v.add_child(flow)
	panel.add_child(v)
	return panel


func _stack_panel(session: MtgcppSession) -> PanelContainer:
	var panel := PanelContainer.new()
	panel.custom_minimum_size = Vector2(0, 124)
	panel.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	var sb := StyleBoxFlat.new()
	sb.bg_color = Palette.SURFACE
	sb.set_corner_radius_all(Palette.RADIUS)
	sb.border_color = Palette.ACCENT
	sb.set_border_width_all(1)
	sb.content_margin_left = 8
	sb.content_margin_right = 8
	sb.content_margin_top = 4
	sb.content_margin_bottom = 4
	panel.add_theme_stylebox_override("panel", sb)
	panel.set_meta("zone_kind", "stack")
	_zone_nodes.append({"node": panel, "seat": -1, "zone": -1, "kind": "stack"})

	var v := VBoxContainer.new()
	v.add_theme_constant_override("separation", 3)
	var cards: Array = session.stack_cards()
	v.add_child(UI.label("STACK (%d)" % cards.size(), 10, Palette.ACCENT))
	var flow := HFlowContainer.new()
	flow.add_theme_constant_override("h_separation", 3)
	if cards.is_empty():
		flow.add_child(UI.label("empty — play a spell or drag a card here (S)", 11,
			Palette.TEXT_FAINT))
	for card: Dictionary in cards:
		flow.add_child(_card_view(card, _my_seat(session), -1, false, CARD_SMALL))
	v.add_child(flow)
	panel.add_child(v)
	return panel


func _library_panel(session: MtgcppSession) -> PanelContainer:
	var panel := PanelContainer.new()
	panel.custom_minimum_size = Vector2(190, 0)
	panel.size_flags_vertical = Control.SIZE_EXPAND_FILL
	var sb := StyleBoxFlat.new()
	sb.bg_color = Palette.SURFACE
	sb.set_corner_radius_all(Palette.RADIUS)
	sb.border_color = Palette.BORDER
	sb.set_border_width_all(1)
	sb.content_margin_left = 6
	sb.content_margin_right = 6
	sb.content_margin_top = 4
	sb.content_margin_bottom = 4
	panel.add_theme_stylebox_override("panel", sb)

	var v := VBoxContainer.new()
	v.add_theme_constant_override("separation", 3)

	var cards: Array = session.library()
	var header := HBoxContainer.new()
	var title := UI.label("LIBRARY (%d)" % cards.size(), 10, Palette.TEXT_FAINT)
	title.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	header.add_child(title)
	var draw := UI.button("Draw", true)
	draw.add_theme_font_size_override("font_size", 11)
	draw.tooltip_text = "Draw the top card into your hand"
	draw.pressed.connect(func() -> void: session.draw_card())
	header.add_child(draw)
	v.add_child(header)

	var scroll := ScrollContainer.new()
	scroll.size_flags_vertical = Control.SIZE_EXPAND_FILL
	scroll.horizontal_scroll_mode = ScrollContainer.SCROLL_MODE_DISABLED
	var list := VBoxContainer.new()
	list.add_theme_constant_override("separation", 2)
	list.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	if cards.is_empty():
		var message := "Loading card database…" if not AppState.db_ready else "No deck loaded"
		list.add_child(UI.label(message, 11, Palette.TEXT_FAINT))
	for card: Dictionary in cards:
		list.add_child(_library_row(session, card))
	scroll.add_child(list)
	v.add_child(scroll)
	panel.add_child(v)
	return panel


func _library_row(session: MtgcppSession, card: Dictionary) -> Button:
	var b := UI.button(str(card.get("name", "")), false, false)
	b.alignment = HORIZONTAL_ALIGNMENT_LEFT
	b.custom_minimum_size.y = 28
	b.add_theme_font_size_override("font_size", 12)
	b.tooltip_text = "%s\nClick to put it into play" % str(card.get("type_line", ""))
	b.pressed.connect(func() -> void: session.place_from_library(str(card.get("id", ""))))
	return b


func _card_view(card: Dictionary, seat: int, zone: int, interactive: bool,
		scale: float) -> Control:
	var view = CardViewScript.new()
	view.configure(card, seat, zone, interactive, scale)
	if interactive:
		view.activated.connect(_on_card_activated)
	if str(_selected.get("id", "")) == str(card.get("id", "")):
		view.set_selected(true)
	_card_views.append(view)
	return view


# A render rebuilds the board; cards whose tapped state flipped to true since
# the last render get the springy rotation instead of a hard 90° snap.
func _animate_tap_flips() -> void:
	var now := {}
	for view in _card_views:
		var id := str(view.get_meta("card_id", ""))
		var tapped := bool(view.card.get("tapped", false))
		now[id] = tapped
		# Animate any tapped card that was not already known as tapped — this
		# includes the first time a pre-tapped card appears, so it never renders
		# upright by mistake.
		if tapped and not bool(_prev_tapped.get(id, false)):
			view.animate_tapped()
	_prev_tapped = now


func _render_reveal(session: MtgcppSession) -> void:
	if session.reveal_request_from() != "":
		var prompt := UI.panel()
		var pv := VBoxContainer.new()
		pv.add_child(UI.label("%s asks to see your hand" % session.reveal_request_from(),
			16, Palette.ACCENT))
		var buttons := HBoxContainer.new()
		var accept := UI.button("Accept", true)
		accept.pressed.connect(func() -> void: session.accept_hand_reveal())
		var deny := UI.button("Deny")
		deny.pressed.connect(func() -> void: session.deny_hand_reveal())
		var dismiss := UI.button("Not now")
		dismiss.pressed.connect(func() -> void: session.dismiss_reveal_prompt())
		buttons.add_child(accept)
		buttons.add_child(deny)
		buttons.add_child(dismiss)
		pv.add_child(buttons)
		prompt.add_child(pv)
		board.add_child(prompt)

	if session.revealed_hand().size() > 0:
		var strip := HBoxContainer.new()
		strip.add_theme_constant_override("separation", 4)
		strip.add_child(UI.label("Revealed hand:", 14, Palette.TEXT_MUTED))
		for card: Dictionary in session.revealed_hand():
			strip.add_child(UI.label(str(card.get("name", "")), 13, Palette.TEXT))
		board.add_child(strip)


# --- selection + verbs ------------------------------------------------------

func _on_card_activated(card: Dictionary, seat: int, zone: int) -> void:
	_selected = {"id": str(card.get("id", "")), "seat": seat,
		"zone": zone, "name": str(card.get("name", ""))}
	_apply_selection()
	_update_verbs()
	_update_hint()


func _apply_selection() -> void:
	var id := str(_selected.get("id", ""))
	for view in _card_views:
		view.set_selected(str(view.get_meta("card_id", "")) == id)


func _open_move_menu(_selected_card: Dictionary = {}) -> void:
	var menu := PopupMenu.new()
	for z in range(ZONE_NAMES.size()):
		menu.add_item(ZONE_NAMES[z], z)
	menu.id_pressed.connect(func(id: int) -> void:
		AppState.session.move_to_zone(_selected["seat"], _selected["id"], id))
	add_child(menu)
	menu.position = get_viewport().get_mouse_position()
	menu.popup()


func _edit_life(seat: int, current: int) -> void:
	var popup := PopupPanel.new()
	var v := VBoxContainer.new()
	v.add_theme_constant_override("separation", 8)
	v.add_child(UI.label("Set life total", 16, Palette.TEXT))
	var spin := SpinBox.new()
	spin.min_value = -999
	spin.max_value = 9999
	spin.value = current
	spin.custom_minimum_size.x = 120
	v.add_child(spin)
	var ok := UI.button("Set", true)
	ok.pressed.connect(func() -> void:
		AppState.session.set_life(seat, int(spin.value))
		popup.hide()
		popup.queue_free())
	v.add_child(ok)
	popup.add_child(v)
	add_child(popup)
	popup.popup_centered()


func _update_hint() -> void:
	if _selected.is_empty():
		hint.text = "Click a card to select it, then Play/act. Drag hand cards to play them where they belong; drag battlefield cards between piles. Shortcuts: P play · T tap · C counter · F flip · K token · S stack · M move."
	else:
		hint.text = "Selected: %s — P play · T tap · C counter · F flip · K token · S stack · M move · Esc clear" % _selected["name"]


# --- keyboard shortcuts -----------------------------------------------------

func _unhandled_key_input(event: InputEvent) -> void:
	if not (event is InputEventKey and event.pressed):
		return
	if event.keycode == KEY_ESCAPE:
		_selected = {}
		_apply_selection()
		_update_verbs()
		_update_hint()
		return
	if _selected.is_empty() or AppState.session == null:
		return
	match event.keycode:
		KEY_P:
			AppState.session.play_card(_selected["id"])
		KEY_T:
			AppState.session.tap(_selected["seat"], _selected["id"])
		KEY_C:
			AppState.session.add_counter(_selected["seat"], _selected["id"])
		KEY_F:
			AppState.session.flip(_selected["seat"], _selected["id"])
		KEY_K:
			AppState.session.create_token(_selected["seat"], Z_CREATURES, "Token")
		KEY_S:
			AppState.session.move_to_stack(_selected["id"])
		KEY_M:
			_open_move_menu()


# --- drag & drop ------------------------------------------------------------

func _can_drop_data(at_position: Vector2, data: Variant) -> bool:
	if AppState.session == null or not (data is Dictionary):
		return false
	if str(data.get("type", "")) != CardViewScript.DRAG_TYPE:
		return false
	if int(data.get("seat", -1)) != _my_seat(AppState.session):
		return false
	return not _zone_at(get_global_rect().position + at_position).is_empty()


func _drop_data(at_position: Vector2, data: Variant) -> void:
	var target: Dictionary = _zone_at(get_global_rect().position + at_position)
	if target.is_empty():
		return
	var card: Dictionary = data["card"]
	var card_id := str(card.get("id", ""))
	if int(data.get("zone", -1)) == -1:
		# A hand card: let the engine route it to the right pile by type.
		AppState.session.play_card(card_id)
	elif target["kind"] == "stack":
		AppState.session.move_to_stack(card_id)
	else:
		AppState.session.move_to_zone(_my_seat(AppState.session), card_id, int(target["zone"]))
	_pending_anim = {"card_id": card_id,
		"from": get_global_rect().position + at_position, "card": card}


func _zone_at(global_pos: Vector2) -> Dictionary:
	for entry: Dictionary in _zone_nodes:
		var node: Control = entry["node"]
		if is_instance_valid(node) and node.get_global_rect().has_point(global_pos):
			return entry
	return {}


func _play_pending_anim() -> void:
	var anim := _pending_anim
	_pending_anim = {}
	if anim.is_empty():
		return
	await get_tree().process_frame
	await get_tree().process_frame
	var target: Control = _find_card_view(str(anim.get("card_id", "")))
	if target == null or AppState.session == null:
		return
	var to := target.get_global_rect().position
	var ghost = CardViewScript.new()
	ghost.configure(anim["card"], _my_seat(AppState.session), -1, false, CARD_ZONE)
	ghost.modulate.a = 0.9
	ghost.z_index = 10
	add_child(ghost)
	ghost.global_position = anim["from"]
	var tween := create_tween().set_parallel(true)
	tween.tween_property(ghost, "global_position", to, 0.22) \
		.set_trans(Tween.TRANS_CUBIC).set_ease(Tween.EASE_OUT)
	tween.tween_property(ghost, "modulate:a", 0.0, 0.22).set_delay(0.06)
	tween.chain().tween_callback(ghost.queue_free)


func _find_card_view(id: String) -> Control:
	return _find_card_view_in(self, id)


func _find_card_view_in(node: Node, id: String) -> Control:
	for child in node.get_children():
		if child.has_meta("card_id") and str(child.get_meta("card_id")) == id:
			return child as Control
		var found := _find_card_view_in(child, id)
		if found != null:
			return found
	return null
