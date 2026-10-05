# Modern procedural card face (Phase 3 UX).
#
# A reusable widget used by the Table: it renders a board card's name, mana cost
# and type line in the app's design language and encodes board state as visible
# affordances (tap rotation, +1/+1 counter badge, token tag, face-down back).
# No network/art is involved: this is the procedural fallback the Phase 4 art
# cache will layer real card images on top of.
#
# Interaction:
#   * `interactive` cards emit `activated` on left click (selection);
#   * `interactive` cards are drag sources — `_get_drag_data` returns a tagged
#     dictionary the Table's drop handler understands;
#   * `set_selected` toggles the accent focus border.
#
# The widget is built entirely in code (no .tscn) so screens can instantiate it
# with `preload(...).new()` without a global class registration.
extends PanelContainer

signal activated(card: Dictionary, seat: int, zone: int)

const FACE_SIZE := Vector2(120.0, 168.0)

const DRAG_TYPE := "mtgcpp_card"

# Mana symbol -> a muted identity colour used for the top strip.
const MANA_COLORS := {
	"W": Color("#e7dcb8"),
	"U": Color("#7fb6d6"),
	"B": Color("#9a83bb"),
	"R": Color("#d97b5a"),
	"G": Color("#79b98a"),
}

var card: Dictionary = {}
var seat := 0
var zone := -1
var interactive := false
var card_scale := 1.0

var _tapped := false
var _selected := false
var _accent: Color = Color("#2b313c")
var _art_key := ""
var _art_shown := false
var _strip: ColorRect
var _body: VBoxContainer
var _art: TextureRect
var _name_label: Label
var _cost_label: Label
var _type_label: Label
var _badge: Label
var _token_tag: Label
var _rot_tween: Tween


func _init() -> void:
	custom_minimum_size = FACE_SIZE
	mouse_filter = Control.MOUSE_FILTER_STOP
	clip_contents = false
	_build()


func configure(value: Dictionary, p_seat: int, p_zone: int, p_interactive: bool,
		p_scale: float = 1.0) -> void:
	card = value
	seat = p_seat
	zone = p_zone
	interactive = p_interactive
	card_scale = p_scale
	set_meta("card_id", str(card.get("id", "")))
	if not card.is_empty():
		_apply()


# The face scales with the space it is given: the hand is largest, own
# battlefield piles mid-size, the opponent/stack smallest.
func _apply_scale() -> void:
	custom_minimum_size = FACE_SIZE * card_scale
	var name_size := 13
	var detail_size := 11
	var badge_size := 14
	if card_scale < 0.95:
		name_size = 11
		detail_size = 9
		badge_size = 12
	if card_scale < 0.72:
		name_size = 10
		detail_size = 8
		badge_size = 11
	_name_label.add_theme_font_size_override("font_size", name_size)
	_cost_label.add_theme_font_size_override("font_size", detail_size + 1)
	_type_label.add_theme_font_size_override("font_size", detail_size)
	_badge.add_theme_font_size_override("font_size", badge_size)
	_token_tag.add_theme_font_size_override("font_size", detail_size - 1)
	# Keep the name on a single line (ellipsis when too long) so it never splits
	# mid-word; the tooltip and the printed art carry the full name.
	_name_label.autowrap_mode = TextServer.AUTOWRAP_OFF
	_name_label.text_overrun_behavior = TextServer.OVERRUN_TRIM_ELLIPSIS
	_name_label.clip_text = true


func set_selected(value: bool) -> void:
	_selected = value
	_restyle()


# --- construction -----------------------------------------------------------

func _build() -> void:
	_restyle()

	# Art sits behind the text body; when it resolves the text is hidden and the
	# real card image fills the face (it already carries name/cost/type).
	_art = TextureRect.new()
	_art.expand_mode = TextureRect.EXPAND_IGNORE_SIZE
	_art.stretch_mode = TextureRect.STRETCH_KEEP_ASPECT_COVERED
	_art.mouse_filter = Control.MOUSE_FILTER_IGNORE
	_art.visible = false
	add_child(_art)

	var body := VBoxContainer.new()
	body.add_theme_constant_override("separation", 3)
	add_child(body)
	_body = body

	_strip = ColorRect.new()
	_strip.custom_minimum_size.y = 3
	body.add_child(_strip)

	var header := HBoxContainer.new()
	header.add_theme_constant_override("separation", 4)
	_name_label = Label.new()
	_name_label.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	_name_label.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_name_label.add_theme_font_size_override("font_size", 12)
	_name_label.add_theme_color_override("font_color", Palette.TEXT)
	_cost_label = Label.new()
	_cost_label.add_theme_font_size_override("font_size", 11)
	_cost_label.add_theme_color_override("font_color", Palette.TEXT_MUTED)
	header.add_child(_name_label)
	header.add_child(_cost_label)
	body.add_child(header)

	_type_label = Label.new()
	_type_label.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	_type_label.add_theme_font_size_override("font_size", 10)
	_type_label.add_theme_color_override("font_color", Palette.TEXT_FAINT)
	body.add_child(_type_label)

	# Overlays: both are resized to the full panel and align their text.
	_badge = Label.new()
	_badge.horizontal_alignment = HORIZONTAL_ALIGNMENT_RIGHT
	_badge.vertical_alignment = VERTICAL_ALIGNMENT_TOP
	_badge.add_theme_font_size_override("font_size", 13)
	_badge.add_theme_color_override("font_color", Palette.ACCENT_HOVER)
	add_child(_badge)

	_token_tag = Label.new()
	_token_tag.horizontal_alignment = HORIZONTAL_ALIGNMENT_RIGHT
	_token_tag.vertical_alignment = VERTICAL_ALIGNMENT_BOTTOM
	_token_tag.add_theme_font_size_override("font_size", 9)
	_token_tag.add_theme_color_override("font_color", Palette.ACCENT)
	add_child(_token_tag)


func _restyle() -> void:
	var sb := StyleBoxFlat.new()
	if bool(card.get("flipped", false)):
		sb.bg_color = Color("#101218")
	else:
		sb.bg_color = Palette.SURFACE_HIGH
	sb.set_corner_radius_all(Palette.RADIUS)
	sb.border_color = Palette.ACCENT if _selected else Palette.BORDER
	sb.set_border_width_all(2 if _selected else 1)
	# Zero margins let the art reach the card edge; text needs the padding.
	var margin := 0 if _art_shown else 7
	sb.content_margin_left = margin
	sb.content_margin_right = margin
	sb.content_margin_top = margin
	sb.content_margin_bottom = margin
	add_theme_stylebox_override("panel", sb)


# --- state ------------------------------------------------------------------

func _apply() -> void:
	var flipped: bool = bool(card.get("flipped", false))
	var is_token: bool = bool(card.get("is_token", false))
	_accent = Palette.ACCENT if is_token else _identity_color()
	_strip.color = _accent

	if flipped:
		_name_label.text = "Card back"
		_name_label.add_theme_color_override("font_color", Palette.TEXT_FAINT)
		_cost_label.text = ""
		_type_label.text = "Face down"
		_badge.visible = false
		_token_tag.visible = false
	else:
		_name_label.text = str(card.get("name", ""))
		_name_label.add_theme_color_override("font_color", Palette.TEXT)
		_cost_label.text = str(card.get("mana_cost", ""))
		var type_line := str(card.get("type_line", ""))
		if is_token and type_line.findn("token") == -1:
			type_line = (type_line + " Token").strip_edges()
		_type_label.text = type_line

		var counters: int = int(card.get("counters", 0))
		_badge.visible = counters > 0
		_badge.text = "+%d" % counters

		# The compact zone face is too small for a separate token ribbon; the
		# violet accent strip and the "Token" type suffix carry that meaning.
		_token_tag.visible = false

	_set_tapped(not flipped and bool(card.get("tapped", false)), false)
	mouse_default_cursor_shape = (
		Control.CURSOR_POINTING_HAND if interactive else Control.CURSOR_ARROW
	)
	tooltip_text = _tooltip()
	_apply_scale()
	_load_art()
	_restyle()


func _ready() -> void:
	# `configure` runs before the node is in the tree, so (re)try art on entry.
	if not card.is_empty():
		_load_art()


# Request the card's art; while it is missing the procedural face stays.
func _load_art() -> void:
	var url := str(card.get("image_url", ""))
	var key := str(card.get("scryfall_id", ""))
	if key.is_empty():
		key = str(card.get("id", ""))
	if url.is_empty() or key.is_empty():
		_show_art(false)
		return
	_art_key = key
	var texture := ArtCache.texture_for(key, url)
	if texture != null:
		_set_art(texture)
		return
	_show_art(false)
	if not ArtCache.art_ready.is_connected(_on_art_ready):
		ArtCache.art_ready.connect(_on_art_ready)


func _on_art_ready(key: String) -> void:
	if key != _art_key or card.is_empty():
		return
	var texture := ArtCache.texture_for(key, "")
	if texture != null:
		_set_art(texture)


func _set_art(texture: Texture2D) -> void:
	_art.texture = texture
	_art.visible = true
	_art_shown = true
	_show_body(false)
	_restyle()


func _show_art(visible_now: bool) -> void:
	_art.visible = visible_now
	if not visible_now:
		_art_shown = false
		_show_body(true)
		_restyle()


func _show_body(visible_now: bool) -> void:
	_body.visible = visible_now
	_strip.visible = visible_now


func _identity_color() -> Color:
	var best := ""
	var best_count := 0
	for symbol: String in MANA_COLORS:
		var count := str(card.get("mana_cost", "")).count("{%s}" % symbol)
		if count > best_count:
			best = symbol
			best_count = count
	if best.is_empty():
		return Palette.BORDER
	return MANA_COLORS[best] as Color


func _tooltip() -> String:
	var name := str(card.get("name", ""))
	var cost := str(card.get("mana_cost", ""))
	var type_line := str(card.get("type_line", ""))
	var lines: Array[String] = []
	if not name.is_empty():
		lines.append(name + ("  " + cost if not cost.is_empty() else ""))
	if not type_line.is_empty():
		lines.append(type_line)
	lines.append("Click to select · drag to move")
	return "\n".join(lines)


func _set_tapped(value: bool, animate: bool) -> void:
	_tapped = value
	var target := deg_to_rad(90.0) if value else 0.0
	if not animate or not is_inside_tree():
		rotation = target
		return
	if _rot_tween != null and _rot_tween.is_valid():
		_rot_tween.kill()
	_rot_tween = create_tween()
	_rot_tween.tween_property(self, "rotation", target, 0.18) \
		.set_trans(Tween.TRANS_BACK).set_ease(Tween.EASE_OUT)


# Turn the tap rotation into a springy 0° -> 90° animation. Used by the Table
# when a card's tapped state flips between renders (the fresh node starts at 0).
func animate_tapped() -> void:
	rotation = 0.0
	_set_tapped(true, true)


func _notification(what: int) -> void:
	if what == NOTIFICATION_RESIZED:
		pivot_offset = size * 0.5


# --- input ------------------------------------------------------------------

func _gui_input(event: InputEvent) -> void:
	if not interactive:
		return
	if event is InputEventMouseButton and event.pressed \
			and event.button_index == MOUSE_BUTTON_LEFT:
		activated.emit(card, seat, zone)


func _get_drag_data(_at_position: Vector2) -> Variant:
	if not interactive or card.is_empty():
		return null
	var preview = get_script().new()
	preview.configure(card, seat, zone, false, card_scale)
	preview.modulate.a = 0.85
	set_drag_preview(preview)
	return {"type": DRAG_TYPE, "card": card, "seat": seat, "zone": zone}
