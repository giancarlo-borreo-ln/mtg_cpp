# Modern design tokens (2026). A clean dark theme: neutral surfaces, a single
# violet accent, sans-serif type, and a small spacing/radius/type scale so every
# screen reads consistently instead of hard-coding hex values ad hoc.
#
# UI/UX best practices encoded here:
#   * a 3-step elevation model (background → surface → surface-high) for depth;
#   * a single accent used only for primary actions, links and selection;
#   * explicit text roles (primary / secondary / faint) with accessible contrast
#     on the dark surfaces;
#   * a fixed radius + spacing scale so components share one rhythm;
#   * a visible focus ring for keyboard navigation.

extends Node

# --- Color roles -------------------------------------------------------------

const BACKGROUND := Color("#0f1115")        # app window
const SURFACE := Color("#171a21")           # panels / cards
const SURFACE_HIGH := Color("#20242d")      # hover / elevated
const SURFACE_PRESSED := Color("#262b36")   # pressed
const BORDER := Color("#2b313c")            # subtle separators / outlines

const TEXT := Color("#e8eaef")              # primary text
const TEXT_MUTED := Color("#9aa2af")        # secondary text
const TEXT_FAINT := Color("#6b7280")        # tertiary / hints

const ACCENT := Color("#7c5cff")            # primary action / selection
const ACCENT_HOVER := Color("#9478ff")      # accent on hover
const ACCENT_PRESSED := Color("#6a4ae0")    # accent while pressed
const ON_ACCENT := Color("#ffffff")         # text on accent fills

const DANGER := Color("#f0556a")            # destructive actions
const DANGER_HOVER := Color("#ff7085")
const SUCCESS := Color("#3ddc97")           # positive / confirmation

const FOCUS := Color("#a78bff")             # focus ring (keyboard nav)

# --- Radius / spacing / type scale -------------------------------------------

const RADIUS := 10                          # corner radius for cards/buttons
const SPACE_SM := 8
const SPACE_MD := 16
const SPACE_LG := 24
const SPACE_XL := 32

const TITLE_SIZE := 26
const HEADING_SIZE := 19
const BODY_SIZE := 17
const SMALL_SIZE := 14
