// Shared geometry helper for menu widgets (M4.2).
#pragma once

#include <SFML/System/Vector2.hpp>

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

} // namespace mtgcpp::core
