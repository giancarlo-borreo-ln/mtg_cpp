extends Control

@onready var content: VBoxContainer = $Margin/Content

func _ready() -> void:
	AppState.profile_changed.connect(_refresh)
	_refresh()


func _refresh(_player_id: String = "") -> void:
	for child in content.get_children():
		content.remove_child(child)
		child.queue_free()
	if AppState.profile_id.is_empty():
		_show_picker()
	else:
		_show_vault()


# --- Profile picker ----------------------------------------------------------

func _show_picker() -> void:
	content.add_child(UI.title("Choose your player"))
	content.add_child(UI.label("Pick who you are to open your deck vault.", 16, Palette.TEXT_MUTED))
	content.add_child(_spacer(16))

	var grid := GridContainer.new()
	grid.columns = 2
	grid.add_theme_constant_override("h_separation", 12)
	grid.add_theme_constant_override("v_separation", 12)
	for profile: Dictionary in MtgcppProfiles.list():
		var b := UI.button(profile["name"])
		b.custom_minimum_size = Vector2(220, 48)
		b.pressed.connect(func() -> void: AppState.select_profile(profile["id"]))
		grid.add_child(b)
	content.add_child(grid)


# --- Deck vault --------------------------------------------------------------

func _show_vault() -> void:
	var player_name := _profile_name(AppState.profile_id)
	content.add_child(UI.title("Deck Vault — %s" % player_name))

	var summaries: Array = AppState.deck_summaries()
	if summaries.is_empty():
		content.add_child(_spacer(12))
		content.add_child(UI.label("No decks yet — create one below.", 16, Palette.TEXT_MUTED))
	else:
		content.add_child(UI.label("Your saved decks:", 15, Palette.TEXT_MUTED))
		content.add_child(_spacer(8))
		for summary: Dictionary in summaries:
			content.add_child(_deck_row(summary))

	content.add_child(_spacer(20))

	var actions := HBoxContainer.new()
	actions.add_theme_constant_override("separation", 12)
	var play := UI.button("Play Online", true)
	play.pressed.connect(func() -> void: AppState.goto("lobby"))
	var new_deck := UI.button("+ New Deck", true)
	new_deck.pressed.connect(func() -> void:
		AppState.begin_new_deck()
		AppState.goto("deck_editor")
	)
	var switch := UI.button("Switch Player")
	switch.pressed.connect(func() -> void: AppState.switch_profile())
	actions.add_child(play)
	actions.add_child(new_deck)
	actions.add_child(switch)
	content.add_child(actions)


func _deck_row(summary: Dictionary) -> Control:
	var row := HBoxContainer.new()
	row.add_theme_constant_override("separation", 8)

	var info := VBoxContainer.new()
	info.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	info.add_child(UI.label(summary["name"], 18, Palette.TEXT))
	var sub := UI.label("%d cards · %d unique · %s" % [
		summary["total_cards"], summary["unique_cards"], summary["format"],
	], 14, Palette.TEXT_MUTED)
	info.add_child(sub)

	var edit := UI.button("Edit")
	edit.pressed.connect(func() -> void:
		AppState.begin_edit_deck(summary["id"])
		AppState.goto("deck_editor")
	)
	var delete := UI.button("Delete", false, true)
	delete.pressed.connect(func() -> void: _confirm_delete(summary))

	row.add_child(info)
	row.add_child(edit)
	row.add_child(delete)

	var panel := UI.panel()
	panel.add_child(row)
	return panel


func _confirm_delete(summary: Dictionary) -> void:
	var dialog := ConfirmationDialog.new()
	dialog.dialog_text = "Delete \"%s\"? This cannot be undone." % summary["name"]
	dialog.get_ok_button().text = "Delete"
	add_child(dialog)
	dialog.confirmed.connect(func() -> void:
		AppState.deck_repo.remove(summary["id"])
		_refresh()
	)
	dialog.popup_centered()


func _profile_name(id: String) -> String:
	for profile: Dictionary in MtgcppProfiles.list():
		if profile["id"] == id:
			return profile["name"]
	return id


func _spacer(height: int) -> Control:
	var c := Control.new()
	c.custom_minimum_size.y = height
	return c
