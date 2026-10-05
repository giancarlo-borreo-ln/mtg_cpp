// Shared geometry helper for menu widgets (M4.2).
#pragma once

#include <SFML/System/String.hpp>
#include <SFML/System/Vector2.hpp>

#include <functional>

namespace mtgcpp::core {

// Every widget anchors at its top-left corner and sizes itself with a width
// and height. This is the one containment rule they all share: a point is
// inside when it is >= the top-left and < the bottom-right, matching
// sf::FloatRect::contains (the right and bottom edges are exclusive, so a
// point exactly on them is outside).
inline bool pointInside(sf::Vector2f topLeft, sf::Vector2f size, sf::Vector2f point) {
  return point.x >= topLeft.x && point.x < topLeft.x + size.x && point.y >= topLeft.y &&
         point.y < topLeft.y + size.y;
}

// The clipboard source the text widgets' paste reads from. Defaults to the OS
// clipboard (sf::Clipboard::getString); tests replace the process-wide reader
// with a fake so paste behavior is verifiable headless (no X11 needed).
using ClipboardReader = std::function<sf::String()>;

// The process-wide clipboard reader, assignable by tests (`clipboardReader() =
// [] { return ...; }`). The default is installed before any assignment.
ClipboardReader &clipboardReader();

} // namespace mtgcpp::core
