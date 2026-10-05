# App shell: hosts the active screen and swaps it on navigation (Phase 3).
#
# UI/UX rules applied here: a single shared Theme (modern sans-serif + design
# tokens) so every screen is consistent; a short fade transition so screen
# switches never flash; and the screen host is a plain Control anchored to the
# window, so the whole app re-flows on resize.

extends Control

const SCREEN_PATHS := {
	"home": "res://scenes/home.tscn",
	"deck_editor": "res://scenes/deck_editor.tscn",
	"lobby": "res://scenes/lobby.tscn",
	"table": "res://scenes/table.tscn",
}

@onready var host: Control = $ScreenHost

var _current_path: String = ""


func _ready() -> void:
	# Solid dark background behind the screens (modern look, no default gray).
	var bg := ColorRect.new()
	bg.color = Palette.BACKGROUND
	bg.set_anchors_preset(Control.PRESET_FULL_RECT)
	add_child(bg)
	move_child(bg, 0)

	theme = _build_theme()
	AppState.navigate.connect(_on_navigate)
	show_screen("home")


func show_screen(screen: String) -> void:
	var path: String = SCREEN_PATHS.get(screen, SCREEN_PATHS["home"])
	if path == _current_path:
		return
	_current_path = path

	for child in host.get_children():
		host.remove_child(child)
		child.queue_free()

	var scene: PackedScene = load(path)
	if scene == null:
		push_error("app: cannot load screen %s" % path)
		return
	var node: Node = scene.instantiate()
	host.add_child(node)
	_fade_in(node)


func _on_navigate(screen: String) -> void:
	show_screen(screen)


# A gentle fade so switching screens reads as a transition, not a flash.
func _fade_in(node: Node) -> void:
	if not (node is CanvasItem):
		return
	var canvas: CanvasItem = node
	canvas.modulate.a = 0.0
	var tween := create_tween()
	tween.tween_property(canvas, "modulate:a", 1.0, 0.15)


# One shared Theme: modern sans-serif (Godot's default font) + the design tokens,
# so screens never set their own fonts/colors ad hoc.
func _build_theme() -> Theme:
	var t := Theme.new()
	t.default_font_size = Palette.BODY_SIZE
	t.set_color("font_color", "Label", Palette.TEXT)
	t.set_color("font_color", "Button", Palette.TEXT)
	t.set_color("font_color", "LineEdit", Palette.TEXT)
	t.set_color("font_placeholder_color", "LineEdit", Palette.TEXT_FAINT)

	# Text inputs: dark surface + subtle border, accent border on focus.
	var line_normal := StyleBoxFlat.new()
	line_normal.bg_color = Palette.SURFACE
	line_normal.set_corner_radius_all(Palette.RADIUS)
	line_normal.border_color = Palette.BORDER
	line_normal.set_border_width_all(1)
	line_normal.content_margin_left = 12
	line_normal.content_margin_right = 12
	line_normal.content_margin_top = 8
	line_normal.content_margin_bottom = 8
	t.set_stylebox("normal", "LineEdit", line_normal)

	var line_focus := line_normal.duplicate()
	line_focus.border_color = Palette.ACCENT
	line_focus.set_border_width_all(2)
	t.set_stylebox("focus", "LineEdit", line_focus)
	return t
