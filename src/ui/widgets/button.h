// A clickable menu button (M4.2).
//
// The interaction model is split so it is unit-testable without a display:
//   * pure state transitions (setMousePosition / mouseButtonPressed /
//     mouseButtonReleased / consumeClicked) are plain data manipulation,
//   * handleEvent() is a thin adapter that translates sf::Event into those
//     pure calls,
//   * draw() is the only part that needs a window, so the headless test suite
//     covers the logic and the running app covers the pixels.
//
// Click semantics follow the OS convention: press inside, then release inside
// = a click. Leaving the button while held cancels the press; releasing
// outside is never a click.
#pragma once

#include "ui/widgets/widget.h"

#include <SFML/Graphics/Font.hpp>
#include <SFML/Graphics/Rect.hpp>
#include <SFML/Graphics/RenderTarget.hpp>
#include <SFML/System/Vector2.hpp>
#include <SFML/Window/Event.hpp>

#include <string>

namespace mtgcpp::core {

class Button {
public:
  // --- Geometry -------------------------------------------------------------
  void setPosition(sf::Vector2f position);
  void setSize(sf::Vector2f size);
  sf::Vector2f position() const { return position_; }
  sf::Vector2f size() const { return size_; }
  sf::FloatRect bounds() const;
  bool contains(sf::Vector2f point) const;

  // --- Label ----------------------------------------------------------------
  void setLabel(std::string label);
  const std::string &label() const { return label_; }

  // Optional second line, drawn muted and smaller under the label (e.g. the
  // "Play as X" hint on a profile card). Empty = no subtitle.
  void setSubLabel(std::string subLabel);
  const std::string &subLabel() const { return subLabel_; }

  // --- Enablement -----------------------------------------------------------
  void setEnabled(bool enabled);
  bool isEnabled() const { return enabled_; }

  // --- Pure mouse transitions (unit-testable) -------------------------------
  // Track the cursor; updates hovered, and cancels a held press that leaves
  // the button (the classic "release outside = cancel" behavior).
  void setMousePosition(sf::Vector2f point);
  // Left button went down. Consumed (returns true) only when the press is
  // inside the button.
  bool mouseButtonPressed(sf::Vector2f point);
  // Left button came up. Consumed if a press is active; queues a click when
  // the release is still inside.
  bool mouseButtonReleased(sf::Vector2f point);
  // True exactly once per completed click (screens poll this each frame).
  bool consumeClicked();

  bool isHovered() const { return hovered_; }
  bool isPressed() const { return pressed_; }

  // --- Events + rendering ---------------------------------------------------
  bool handleEvent(const sf::Event &event);
  // `scale` is the responsive UI scale (ui/layout.h): fonts and the outline
  // grow/shrink with the window height so the button reads well on any monitor.
  void draw(sf::RenderTarget &target, const sf::Font &font, float scale) const;

private:
  sf::Vector2f position_{0.f, 0.f};
  sf::Vector2f size_{0.f, 0.f};
  std::string label_;
  std::string subLabel_;
  bool enabled_ = true;
  bool hovered_ = false;
  bool pressed_ = false;
  bool clickQueued_ = false;
};

} // namespace mtgcpp::core
