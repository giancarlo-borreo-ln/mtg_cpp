extends Control

@onready var content: VBoxContainer = $Margin/Content

var _deck_buttons: Array[Button] = []
var _last_players: int = -1
var _last_status: int = -1
var _last_my_deck: String = ""
var _last_their_deck: String = ""


func _ready() -> void:
	_show_connect()


func _process(_delta: float) -> void:
	var session: MtgcppSession = AppState.session
	if session == null or not session.connected():
		return
	session.pump()

	# Detect view-state changes and refresh the room panel only when something
	# actually moved (avoids rebuilding the tree every frame).
	var changed := (session.players().size() != _last_players
			or session.status() != _last_status
			or session.my_deck_name() != _last_my_deck
			or session.their_deck_name() != _last_their_deck)
	if changed:
		_last_players = session.players().size()
		_last_status = session.status()
		_last_my_deck = session.my_deck_name()
		_last_their_deck = session.their_deck_name()
		_show_room()

	if session.can_start_table():
		AppState.goto("table")


func _show_connect() -> void:
	_clear()

	content.add_child(UI.title("Lobby"))
	content.add_child(UI.label("Host a game, or join a friend's IP:PORT.", 16, Palette.TEXT_MUTED))
	content.add_child(_spacer(12))

	var create := UI.button("Create Room", true)
	create.pressed.connect(_on_create)
	content.add_child(create)

	content.add_child(_spacer(8))
	var join_row := HBoxContainer.new()
	join_row.add_theme_constant_override("separation", 8)
	var join_input := LineEdit.new()
	join_input.placeholder_text = "IP:PORT (e.g. 192.168.1.5:7500)"
	join_input.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	join_input.text_submitted.connect(func(t: String) -> void: _on_join(t))
	var join := UI.button("Join Room", true)
	join.pressed.connect(func() -> void: _on_join(join_input.text))
	join_row.add_child(join_input)
	join_row.add_child(join)
	content.add_child(join_row)

	content.add_child(_spacer(8))
	var sandbox := UI.button("Sandbox (local empty table)")
	sandbox.tooltip_text = "Open a local table with no opponent (hosts an embedded relay)"
	sandbox.pressed.connect(_on_sandbox)
	content.add_child(sandbox)

	content.add_child(_spacer(8))
	var back := UI.button("← Back")
	back.pressed.connect(func() -> void: AppState.goto("home"))
	content.add_child(back)


func _show_room() -> void:
	_clear()
	var session: MtgcppSession = AppState.session

	var heading := "Room"
	if session.share_address() != "":
		heading += " — %s" % session.share_address()
	content.add_child(UI.title(heading))

	content.add_child(UI.label("Players: %d" % session.players().size(), 16, Palette.TEXT))
	content.add_child(UI.label("You: %s" % session.player_id(), 14, Palette.TEXT_MUTED))

	if session.my_deck_name() == "":
		_show_deck_picker()
	else:
		content.add_child(UI.label("Your deck: %s" % session.my_deck_name(), 16, Palette.TEXT))
		if session.their_deck_name() == "":
			content.add_child(UI.label("Waiting for your opponent to pick a deck…", 14, Palette.TEXT_MUTED))
		else:
			content.add_child(UI.label("Opponent deck: %s" % session.their_deck_name(), 16, Palette.TEXT_MUTED))
			content.add_child(UI.label("Starting the table…", 15, Palette.ACCENT))

	if session.last_error() != "":
		content.add_child(UI.label(session.last_error(), 14, Palette.DANGER))

	content.add_child(_spacer(12))
	var leave := UI.button("Leave Room", false, true)
	leave.pressed.connect(_on_leave)
	content.add_child(leave)


func _show_deck_picker() -> void:
	content.add_child(UI.label("Pick your deck:", 16, Palette.TEXT))
	var summaries: Array = AppState.deck_summaries()
	if summaries.is_empty():
		content.add_child(UI.label("No decks saved — create one in the deck editor first.", 14, Palette.TEXT_MUTED))
		return
	for summary: Dictionary in summaries:
		var playable: bool = int(summary["total_cards"]) >= 60
		var label := "%s  (%d cards)" % [summary["name"], summary["total_cards"]]
		var b := UI.button(label)
		b.disabled = not playable
		b.pressed.connect(func() -> void: _choose_deck(summary["id"]))
		content.add_child(b)


func _choose_deck(deck_id: String) -> void:
	var read: Dictionary = AppState.deck_repo.read(deck_id)
	if not bool(read.get("ok", false)):
		return
	AppState.session.choose_deck(read["deck"])


func _on_create() -> void:
	var result: Dictionary = AppState.start_host()
	if bool(result.get("ok", false)):
		_show_room()
	else:
		content.add_child(UI.label(result.get("error", "Could not start the room"), 14, Palette.DANGER))


# Sandbox: host a local room, load the bundled landfall deck as a library, and
# jump straight to the table (no opponent).
func _on_sandbox() -> void:
	var result: Dictionary = AppState.start_sandbox()
	if bool(result.get("ok", false)):
		AppState.goto("table")
	else:
		content.add_child(UI.label(result.get("error", "Could not start the room"), 14, Palette.DANGER))


func _on_join(address: String) -> void:
	var result: Dictionary = AppState.start_guest(address)
	if bool(result.get("ok", false)):
		_show_room()
	else:
		content.add_child(UI.label(result.get("error", "Could not join"), 14, Palette.DANGER))


func _on_leave() -> void:
	AppState.stop_session()
	_last_players = -1
	_last_status = -1
	_last_my_deck = ""
	_last_their_deck = ""
	_show_connect()


func _clear() -> void:
	for child in content.get_children():
		content.remove_child(child)
		child.queue_free()


func _spacer(height: int) -> Control:
	var c := Control.new()
	c.custom_minimum_size.y = height
	return c
