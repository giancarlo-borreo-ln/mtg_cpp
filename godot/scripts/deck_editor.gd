extends Control

const IMPORT_PROMPT := "Paste an MTG Arena deck export here (e.g. \"4 Lightning Bolt (CLU) 141\")..."

@onready var top_bar: HBoxContainer = $Margin/VBox/TopBar
@onready var name_input: LineEdit = $Margin/VBox/TopBar/NameInput
@onready var body: VBoxContainer = $Margin/VBox/Body

var _deck: Array[MtgcppCard] = []
var _results: Array[MtgcppCard] = []
var _preview: MtgcppImportPreview = null
var _replace_results: Array[MtgcppCard] = []
var _import_view := false


func _ready() -> void:
	_deck = AppState.editor_cards
	name_input.text = AppState.editor_name if AppState.editor_name != "" else "Untitled Deck"
	name_input.text_changed.connect(_on_name_changed)
	_build_top_bar()
	_render()


func _on_name_changed(value: String) -> void:
	AppState.editor_name = value


func _build_top_bar() -> void:
	for child in top_bar.get_children():
		top_bar.remove_child(child)
		child.queue_free()

	var back := UI.button("← Back")
	back.pressed.connect(func() -> void: AppState.goto("home"))
	top_bar.add_child(back)

	name_input.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	top_bar.add_child(name_input)

	var save := UI.button("Save", true)
	save.pressed.connect(_save)
	var export := UI.button("Export")
	export.pressed.connect(_export)
	top_bar.add_child(save)
	top_bar.add_child(export)


func _render() -> void:
	for child in body.get_children():
		body.remove_child(child)
		child.queue_free()
	if _import_view:
		_render_import()
	else:
		_render_build()


func _clear_and_set_import_view(on: bool) -> void:
	_import_view = on
	_render()


# --- Build view --------------------------------------------------------------

func _render_build() -> void:
	# Search row.
	var search_row := HBoxContainer.new()
	search_row.add_theme_constant_override("separation", 8)
	var search := LineEdit.new()
	search.placeholder_text = "Search cards (name, (SET) code, or number)"
	search.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	search.text_submitted.connect(func(text: String) -> void: _run_search(text))
	var go := UI.button("Search")
	go.pressed.connect(func() -> void: _run_search(search.text))
	var imp := UI.button("Import")
	imp.pressed.connect(func() -> void: _clear_and_set_import_view(true))
	search_row.add_child(search)
	search_row.add_child(go)
	search_row.add_child(imp)
	body.add_child(search_row)
	body.add_child(_spacer(8))

	# Two panes.
	var panes := HBoxContainer.new()
	panes.add_theme_constant_override("separation", 12)
	panes.size_flags_vertical = Control.SIZE_EXPAND_FILL
	panes.add_child(_results_pane())
	panes.add_child(_deck_pane())
	body.add_child(panes)


func _results_pane() -> Control:
	var panel := UI.panel()
	panel.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	var v := VBoxContainer.new()
	v.add_theme_constant_override("separation", 4)
	v.add_child(UI.label("Results", 18, Palette.ACCENT))
	if not AppState.db_ready:
		v.add_child(UI.label("Card database not loaded.", 14, Palette.TEXT_MUTED))
	elif _results.is_empty():
		v.add_child(UI.label("No results.", 14, Palette.TEXT_MUTED))
	else:
		for card: MtgcppCard in _results:
			v.add_child(_result_row(card))
	panel.add_child(v)
	return panel


func _result_row(card: MtgcppCard) -> Control:
	var b := Button.new()
	b.text = "%s   %s" % [card.get_name(), card.get_type_line()]
	b.alignment = HORIZONTAL_ALIGNMENT_LEFT
	b.add_theme_font_size_override("font_size", 15)
	b.custom_minimum_size.y = 34
	b.pressed.connect(func() -> void: _add_card(card))
	return b


func _deck_pane() -> Control:
	var panel := UI.panel()
	panel.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	var v := VBoxContainer.new()
	v.add_theme_constant_override("separation", 4)
	var totals := MtgcppDeckEditor.totals(_deck)
	v.add_child(UI.label("Deck (%d cards)" % totals["total"], 18, Palette.ACCENT))
	if _deck.is_empty():
		v.add_child(UI.label("Click a result to add it.", 14, Palette.TEXT_MUTED))
	else:
		for card: MtgcppCard in _deck:
			v.add_child(_deck_row(card))
	panel.add_child(v)
	return panel


func _deck_row(card: MtgcppCard) -> Control:
	var row := HBoxContainer.new()
	row.add_theme_constant_override("separation", 6)

	var name := UI.label("%d× %s" % [card.get_quantity(), card.get_name()], 15, Palette.TEXT)
	name.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	row.add_child(name)

	var minus := UI.button("−")
	minus.custom_minimum_size = Vector2(32, 32)
	minus.pressed.connect(func() -> void: _change_qty(card, -1))
	var plus := UI.button("+")
	plus.custom_minimum_size = Vector2(32, 32)
	plus.pressed.connect(func() -> void: _change_qty(card, +1))
	var remove := UI.button("✕")
	remove.custom_minimum_size = Vector2(32, 32)
	remove.add_theme_color_override("font_color", Palette.DANGER)
	remove.pressed.connect(func() -> void:
		_deck = MtgcppDeckEditor.remove_card(_deck, card.key())
		AppState.editor_cards = _deck
		_render()
	)

	row.add_child(minus)
	row.add_child(plus)
	row.add_child(remove)
	return row


func _add_card(card: MtgcppCard) -> void:
	_deck = MtgcppDeckEditor.add_card(_deck, card)
	AppState.editor_cards = _deck
	_render()


func _change_qty(card: MtgcppCard, delta: int) -> void:
	var qty := card.get_quantity() + delta
	_deck = MtgcppDeckEditor.set_quantity(_deck, card.key(), qty)
	AppState.editor_cards = _deck
	_render()


func _run_search(query: String) -> void:
	if not AppState.db_ready:
		_results = []
		_render()
		return
	_results = AppState.card_db.search(query, 50)
	_render()


# --- Import view -------------------------------------------------------------

func _render_import() -> void:
	var cancel := UI.button("← Cancel")
	cancel.pressed.connect(func() -> void: _clear_and_set_import_view(false))
	body.add_child(cancel)

	if _preview == null:
		_render_import_prompt()
	else:
		_render_import_preview()


func _render_import_prompt() -> void:
	var area := TextEdit.new()
	area.custom_minimum_size.y = 220
	area.placeholder_text = IMPORT_PROMPT
	body.add_child(area)

	var row := HBoxContainer.new()
	var imp := UI.button("Import")
	imp.pressed.connect(func() -> void: _do_import(area.text))
	row.add_child(imp)
	body.add_child(row)


func _do_import(text: String) -> void:
	var result: Dictionary = AppState.card_db.import_deck(text)
	if not bool(result.get("ok", false)):
		body.add_child(UI.label("Import failed: %s" % result.get("error", ""), 14, Palette.DANGER))
		return
	_preview = result["preview"]
	_render()


func _render_import_preview() -> void:
	body.add_child(UI.label("Review import", 20, Palette.ACCENT))

	var resolved := UI.panel()
	resolved.size_flags_vertical = Control.SIZE_EXPAND_FILL
	var rv := VBoxContainer.new()
	rv.add_child(UI.label("Resolved (%d)" % _preview.resolved().size(), 16, Palette.TEXT))
	for card: MtgcppCard in _preview.resolved():
		rv.add_child(UI.label("%d× %s" % [card.get_quantity(), card.get_name()], 14, Palette.TEXT_MUTED))
	resolved.add_child(rv)
	body.add_child(resolved)

	var missing_list := _preview.missing()
	if not missing_list.is_empty():
		body.add_child(UI.label("Needs attention (%d)" % missing_list.size(), 16, Palette.DANGER))
		for i in range(missing_list.size()):
			body.add_child(_missing_row(i, missing_list[i]))

	# Replace strip (when a missing entry is selected).
	if _preview.has_active():
		var strip := HBoxContainer.new()
		var replace_input := LineEdit.new()
		replace_input.placeholder_text = "Search a replacement..."
		replace_input.size_flags_horizontal = Control.SIZE_EXPAND_FILL
		replace_input.text_submitted.connect(func(t: String) -> void: _replace_search(t))
		strip.add_child(replace_input)
		body.add_child(strip)
		for card: MtgcppCard in _replace_results:
			var b := UI.button("%s — %s" % [card.get_name(), card.get_type_line()])
			b.pressed.connect(func() -> void:
				_preview.replace_missing(card)
				_replace_results = []
				_render()
			)
			body.add_child(b)

	var footer := HBoxContainer.new()
	footer.add_theme_constant_override("separation", 12)
	var confirm := UI.button("Confirm Import", true)
	confirm.disabled = not _preview.can_confirm()
	confirm.pressed.connect(_confirm_import)
	var cancel := UI.button("Cancel")
	cancel.pressed.connect(func() -> void:
		_preview = null
		_replace_results = []
		_clear_and_set_import_view(false)
	)
	footer.add_child(confirm)
	footer.add_child(cancel)
	body.add_child(footer)


func _missing_row(index: int, missing: Dictionary) -> Control:
	var row := HBoxContainer.new()
	row.add_theme_constant_override("separation", 6)
	var label := UI.label("%d× %s" % [missing["quantity"], missing["name"]], 15, Palette.TEXT)
	label.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	row.add_child(label)
	var replace := UI.button("Replace")
	replace.pressed.connect(func() -> void:
		_preview.select_missing(index)
		_replace_results = []
		_render()
	)
	var remove := UI.button("Remove")
	remove.pressed.connect(func() -> void:
		_preview.remove_missing(index)
		_render()
	)
	row.add_child(replace)
	row.add_child(remove)
	return row


func _replace_search(query: String) -> void:
	if not AppState.db_ready:
		_replace_results = []
		_render()
		return
	_replace_results = AppState.card_db.search(query, 20)
	_render()


func _confirm_import() -> void:
	var resolved := _preview.confirm()
	if resolved.is_empty():
		return
	_deck = resolved
	AppState.editor_cards = _deck
	_preview = null
	_replace_results = []
	_clear_and_set_import_view(false)


# --- Save / export -----------------------------------------------------------

func _save() -> void:
	var deck := MtgcppDeck.new()
	deck.set_name(name_input.text if name_input.text != "" else "Untitled Deck")
	deck.set_format("Other")
	deck.set_cards(_deck)
	var result: Dictionary
	if AppState.editing_deck_id == "":
		result = AppState.deck_repo.create(deck)
		if bool(result.get("ok", false)):
			AppState.editing_deck_id = result["deck"].get_id()
	else:
		deck = AppState.deck_repo.read(AppState.editing_deck_id)["deck"] as MtgcppDeck
		deck.set_name(name_input.text if name_input.text != "" else "Untitled Deck")
		deck.set_cards(_deck)
		result = AppState.deck_repo.update(deck)
	if bool(result.get("ok", false)):
		AppState.goto("home")
	else:
		body.add_child(UI.label("Save failed: %s" % result.get("error", ""), 14, Palette.DANGER))


func _export() -> void:
	var text := MtgcppArena.to_text(_deck)
	DisplayServer.clipboard_set(text)


func _spacer(height: int) -> Control:
	var c := Control.new()
	c.custom_minimum_size.y = height
	return c
