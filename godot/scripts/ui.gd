# Modern component factory (autoloaded as `UI`). One place builds every themed
# control so the whole app shares the same visual language.
#
# Best practices applied:
#   * two button variants — primary (accent fill) and default (surface + border)
#     — with distinct hover / pressed / disabled states and a visible focus ring;
#   * a generous minimum height so controls are comfortably clickable;
#   * panels use the elevation model (surface over background) with a subtle
#     border and rounded corners.

extends Node

const MIN_BUTTON_HEIGHT := 44


# `primary` = filled accent button (the single main action on a screen);
# `danger` = destructive action. Otherwise a neutral surface button. All are
# keyboard-focusable.
func button(text: String, primary: bool = false, danger: bool = false) -> Button:
	var b := Button.new()
	b.text = text
	b.custom_minimum_size.y = MIN_BUTTON_HEIGHT
	b.add_theme_font_size_override("font_size", Palette.BODY_SIZE)

	if primary:
		b.add_theme_stylebox_override("normal", _flat(Palette.ACCENT))
		b.add_theme_stylebox_override("hover", _flat(Palette.ACCENT_HOVER))
		b.add_theme_stylebox_override("pressed", _flat(Palette.ACCENT_PRESSED))
		b.add_theme_stylebox_override("disabled", _flat(Palette.SURFACE_HIGH))
		b.add_theme_color_override("font_color", Palette.ON_ACCENT)
		b.add_theme_color_override("font_hover_color", Palette.ON_ACCENT)
		b.add_theme_color_override("font_pressed_color", Palette.ON_ACCENT)
		b.add_theme_color_override("font_disabled_color", Palette.TEXT_FAINT)
	elif danger:
		b.add_theme_stylebox_override("normal", _flat(Palette.SURFACE))
		b.add_theme_stylebox_override("hover", _flat(Palette.SURFACE_HIGH))
		b.add_theme_stylebox_override("pressed", _flat(Palette.SURFACE_PRESSED))
		b.add_theme_stylebox_override("disabled", _flat(Color(0, 0, 0, 0)))
		b.add_theme_color_override("font_color", Palette.DANGER)
		b.add_theme_color_override("font_hover_color", Palette.DANGER_HOVER)
		b.add_theme_color_override("font_pressed_color", Palette.DANGER)
		b.add_theme_color_override("font_disabled_color", Palette.TEXT_FAINT)
	else:
		b.add_theme_stylebox_override("normal", _flat(Palette.SURFACE))
		b.add_theme_stylebox_override("hover", _flat(Palette.SURFACE_HIGH))
		b.add_theme_stylebox_override("pressed", _flat(Palette.SURFACE_PRESSED))
		b.add_theme_stylebox_override("disabled", _flat(Color(0, 0, 0, 0)))
		b.add_theme_color_override("font_color", Palette.TEXT)
		b.add_theme_color_override("font_hover_color", Palette.TEXT)
		b.add_theme_color_override("font_pressed_color", Palette.TEXT)
		b.add_theme_color_override("font_disabled_color", Palette.TEXT_FAINT)

	b.add_theme_stylebox_override("focus", _focus_ring())
	return b


func label(text: String, size: int = Palette.BODY_SIZE, color: Color = Palette.TEXT) -> Label:
	var l := Label.new()
	l.text = text
	l.add_theme_font_size_override("font_size", size)
	l.add_theme_color_override("font_color", color)
	return l


func title(text: String) -> Label:
	return label(text, Palette.TITLE_SIZE, Palette.TEXT)


func heading(text: String) -> Label:
	return label(text, Palette.HEADING_SIZE, Palette.TEXT)


# A surface card/panel with a subtle border and rounded corners.
func panel() -> PanelContainer:
	var p := PanelContainer.new()
	p.add_theme_stylebox_override("panel", _flat(Palette.SURFACE))
	return p


func _flat(fill: Color) -> StyleBoxFlat:
	var sb := StyleBoxFlat.new()
	sb.bg_color = fill
	sb.set_corner_radius_all(Palette.RADIUS)
	sb.content_margin_left = 16
	sb.content_margin_right = 16
	sb.content_margin_top = 10
	sb.content_margin_bottom = 10
	return sb


func _focus_ring() -> StyleBoxFlat:
	var sb := StyleBoxFlat.new()
	sb.bg_color = Palette.SURFACE_HIGH
	sb.set_corner_radius_all(Palette.RADIUS)
	sb.border_color = Palette.FOCUS
	sb.set_border_width_all(2)
	sb.content_margin_left = 14
	sb.content_margin_right = 14
	sb.content_margin_top = 8
	sb.content_margin_bottom = 8
	return sb
