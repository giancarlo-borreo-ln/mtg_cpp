// Cursor states (M10.3 polish).
//
// Screens expose a pure `cursorAt(point)` hit-test so the App can set the OS
// cursor without coupling the window to widget internals: `Hand` over anything
// clickable (buttons, cards, rows, life rings), `Text` over editable text, and
// `Arrow` everywhere else. The App loads one `sf::Cursor` per kind and applies
// the active screen's answer each frame. The enum keeps the mapping
// headless-testable (no display needed).
#pragma once

#include <cstdint>

namespace mtgcpp::core {

enum class CursorKind : std::uint8_t { Arrow, Hand, Text };

} // namespace mtgcpp::core
