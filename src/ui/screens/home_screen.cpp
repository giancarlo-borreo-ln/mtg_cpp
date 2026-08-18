// HomeScreen implementation (M4.3): picker + deck vault, responsive layout.

#include "ui/screens/home_screen.h"

#include "ui/layout.h"
#include "ui/theme.h"

#include <SFML/Graphics/RectangleShape.hpp>
#include <SFML/Graphics/Text.hpp>

#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace mtgcpp::core {

namespace {
// Layout constants, authored in design pixels and scaled by uiScale().
constexpr float kPad = 24.f;           // content padding around the panels
constexpr float kGap = 16.f;           // grid / button-row gap
constexpr float kMinCardWidth = 200.f; // profile card minimum (auto-fill minmax)
constexpr float kButtonHeight = 40.f;  // vault action buttons
constexpr float kListRowHeight = 64.f; // deck list rows

// The vault is a master-detail split: the deck list keeps this share of the
// pane width and the highlighted-deck detail pane gets the rest.
constexpr float kListShare = 0.62f;

// MTG card aspect (width : height ~ 63 : 88); the detail picture keeps it so
// the placeholder already looks like a real card.
constexpr float kCardAspectH = 88.f / 63.f;
} // namespace

HomeScreen::HomeScreen() {
  // Profile cards: the name plus a "Play as X" hint, one per fixed identity.
  std::size_t index = 0;
  for (const PlayerProfile &profile : playerProfiles()) {
    Button &button = profileButtons_.at(index);
    button.setLabel(profile.name);
    button.setSubLabel("Play as " + profile.name);
    ++index;
  }
  playButton_.setLabel("Play Online");
  newDeckButton_.setLabel("+ New Deck");
  editButton_.setLabel("Edit deck");
  switchButton_.setLabel("Switch player");
}

void HomeScreen::setPlayerId(std::optional<std::string> playerId) {
  playerId_ = std::move(playerId);
  deleteIndex_.reset();
  pendingAction_ = HomeAction::None;
}

void HomeScreen::setDecks(std::vector<DeckSummary> decks) {
  deleteIndex_.reset();
  deckList_.setDecks(std::move(decks));
}

const DeckSummary *HomeScreen::highlightedDeck() const {
  const std::optional<std::size_t> index = deckList_.selectedIndex();
  if (!index.has_value()) {
    return nullptr; // nothing highlighted yet — the detail pane shows a hint
  }
  return &deckList_.decks().at(index.value());
}

void HomeScreen::setAction(HomeAction action) {
  // Keep the first action of a frame so rapid clicks cannot interleave two
  // file operations; the App drains it at most once per poll.
  if (pendingAction_ == HomeAction::None) {
    pendingAction_ = action;
  }
}

void HomeScreen::relayout(const sf::FloatRect &content, float scale) {
  scale_ = scale;
  if (isProfilePicker()) {
    relayoutPicker(content, scale);
  } else {
    relayoutVault(content, scale);
  }
}

void HomeScreen::relayoutPicker(const sf::FloatRect &content, float scale) {
  // A framed panel holds the grid; the panel fills the content band.
  const sf::FloatRect inner = inset(content, kPad * scale);
  gridPanel_ = inner;
  // Responsive columns: as many as fit a minimum card width (CSS auto-fill
  // minmax). 4 on a normal window, 2 when narrow, 1 when very narrow.
  const std::size_t columns =
      responsiveColumns(inner.width, kMinCardWidth * scale, kGap * scale, profileButtons_.size());
  const std::vector<sf::FloatRect> cells =
      gridCells(inner, profileButtons_.size(), columns, kGap * scale);
  for (std::size_t i = 0; i < profileButtons_.size(); ++i) {
    Button &button = profileButtons_.at(i);
    const sf::FloatRect cell = cells.at(i);
    button.setPosition({cell.left, cell.top});
    button.setSize({cell.width, cell.height});
  }
}

void HomeScreen::relayoutVault(const sf::FloatRect &content, float scale) {
  const sf::FloatRect inner = inset(content, kPad * scale);
  // Action buttons share the top row evenly (a flex row of `1fr`s).
  const sf::FloatRect buttonRow{inner.left, inner.top, inner.width, kButtonHeight * scale};
  const std::vector<sf::FloatRect> buttons = rowOf(buttonRow, 4, kGap * scale);
  playButton_.setPosition({buttons.at(0).left, buttons.at(0).top});
  playButton_.setSize({buttons.at(0).width, buttons.at(0).height});
  newDeckButton_.setPosition({buttons.at(1).left, buttons.at(1).top});
  newDeckButton_.setSize({buttons.at(1).width, buttons.at(1).height});
  editButton_.setPosition({buttons.at(2).left, buttons.at(2).top});
  editButton_.setSize({buttons.at(2).width, buttons.at(2).height});
  switchButton_.setPosition({buttons.at(3).left, buttons.at(3).top});
  switchButton_.setSize({buttons.at(3).width, buttons.at(3).height});

  // Master-detail split below the buttons: the deck list keeps kListShare of
  // the width, the highlighted-deck detail pane takes the rest.
  const float paneTop = buttonRow.top + buttonRow.height + (kGap * scale);
  const float paneHeight = inner.top + inner.height - paneTop;
  const sf::FloatRect pane{inner.left, paneTop, inner.width, paneHeight};
  const float gap = kGap * scale;
  const float listWidth = pane.width * kListShare;
  const float detailWidth = pane.width - listWidth - gap;
  listRect_ = {pane.left, pane.top, listWidth, paneHeight};
  detailRect_ = {pane.left + listWidth + gap, pane.top, detailWidth, paneHeight};
  deckList_.setPosition({listRect_.left, listRect_.top});
  deckList_.setSize({listRect_.width, listRect_.height});
  deckList_.setScale(scale);
  deckList_.setRowHeight(kListRowHeight * scale);
}

bool HomeScreen::routeEvent(const sf::Event &event) {
  if (isProfilePicker()) {
    // Every profile card needs the cursor position to track hover; a move is
    // passive (never consumed) so the router still sees it.
    if (event.type == sf::Event::MouseMoved) {
      const sf::Vector2f point{static_cast<float>(event.mouseMove.x),
                               static_cast<float>(event.mouseMove.y)};
      for (Button &button : profileButtons_) {
        button.setMousePosition(point);
      }
      return false;
    }
    // First card that consumes the event wins (the one the click landed on).
    for (Button &button : profileButtons_) {
      if (button.handleEvent(event)) {
        return true;
      }
    }
    return false;
  }

  // Vault: the deck list first (it owns the rows), then the action buttons.
  if (deckList_.handleEvent(event)) {
    return true;
  }
  if (switchButton_.handleEvent(event)) {
    return true;
  }
  if (editButton_.handleEvent(event)) {
    return true;
  }
  if (newDeckButton_.handleEvent(event)) {
    return true;
  }
  return playButton_.handleEvent(event);
}

HomeAction HomeScreen::pollAction() {
  // Vault drains first: a Delete-button click queues only a deletion, while
  // the action buttons queue their own actions.
  if (const std::optional<std::size_t> deleted = deckList_.consumeDelete(); deleted.has_value()) {
    deleteIndex_ = deleted;
    return HomeAction::DeleteDeck;
  }
  if (playButton_.consumeClicked()) {
    setAction(HomeAction::PlayOnline);
  }
  if (newDeckButton_.consumeClicked()) {
    setAction(HomeAction::NewDeck);
  }
  // Editing needs a deck to open; without a highlight the button is inert.
  if (editButton_.consumeClicked() && highlightedDeck() != nullptr) {
    setAction(HomeAction::EditDeck);
  }
  if (switchButton_.consumeClicked()) {
    setAction(HomeAction::SwitchPlayer);
  }
  if (pendingAction_ != HomeAction::None) {
    const HomeAction action = pendingAction_;
    pendingAction_ = HomeAction::None;
    return action;
  }
  if (isProfilePicker()) {
    // A completed profile-card click selects that profile and flips to the
    // vault (the App persists it and reloads the decks). Range-for over the
    // span; the button index is bounded by the fixed profile count.
    std::size_t i = 0;
    for (const PlayerProfile &profile : playerProfiles()) {
      if (profileButtons_.at(i).consumeClicked()) {
        setPlayerId(profile.id);
        return HomeAction::SelectProfile;
      }
      ++i;
    }
  }
  return HomeAction::None;
}

void HomeScreen::draw(sf::RenderTarget &target, const sf::Font &font,
                      const sf::Font &boldFont) const {
  if (isProfilePicker()) {
    // The framed panel behind the picker grid.
    sf::RectangleShape panel({gridPanel_.width, gridPanel_.height});
    panel.setPosition({gridPanel_.left, gridPanel_.top});
    panel.setFillColor(menuPalette().panel);
    panel.setOutlineThickness(2.f);
    panel.setOutlineColor(menuPalette().gold);
    target.draw(panel);
    for (const Button &button : profileButtons_) {
      button.draw(target, boldFont, scale_);
    }
    return;
  }

  // Vault: action buttons, then either the deck list or the empty state, plus
  // the highlighted-deck detail pane on the right.
  playButton_.draw(target, boldFont, scale_);
  newDeckButton_.draw(target, boldFont, scale_);
  editButton_.draw(target, boldFont, scale_);
  switchButton_.draw(target, boldFont, scale_);
  if (hasDecks()) {
    deckList_.draw(target, font, boldFont);
  } else {
    // Empty state: the same framed slot the list would occupy, with a hint.
    // ASCII only (sf::String decodes const char* through the C locale).
    sf::RectangleShape panel({listRect_.width, listRect_.height});
    panel.setPosition({listRect_.left, listRect_.top});
    panel.setFillColor(menuPalette().background);
    panel.setOutlineThickness(2.f);
    panel.setOutlineColor(menuPalette().gold);
    target.draw(panel);
    sf::Text empty("No decks yet - create your first one.", font,
                   static_cast<unsigned>(16.f * scale_));
    empty.setFillColor(menuPalette().muted);
    const sf::FloatRect bounds = empty.getLocalBounds();
    empty.setPosition({listRect_.left + ((listRect_.width - bounds.width) / 2.f) - bounds.left,
                       listRect_.top + ((listRect_.height - bounds.height) / 2.f) - bounds.top});
    target.draw(empty);
  }
  drawDeckDetail(target, font, boldFont);
}

void HomeScreen::drawDeckDetail(sf::RenderTarget &target, const sf::Font &font,
                                const sf::Font &boldFont) const {
  // The detail pane frame (drawn always, so the pane reads as a fixed slot).
  sf::RectangleShape panel({detailRect_.width, detailRect_.height});
  panel.setPosition({detailRect_.left, detailRect_.top});
  panel.setFillColor(menuPalette().background);
  panel.setOutlineThickness(2.f);
  panel.setOutlineColor(menuPalette().gold);
  target.draw(panel);
  const sf::FloatRect inner = inset(detailRect_, kPad * scale_);

  const DeckSummary *deck = highlightedDeck();
  if (deck == nullptr) {
    // Nothing highlighted yet: a gentle prompt instead of an empty box.
    sf::Text hint("Select a deck to see its details.", font, static_cast<unsigned>(14.f * scale_));
    hint.setFillColor(menuPalette().muted);
    const sf::FloatRect bounds = hint.getLocalBounds();
    hint.setPosition({detailRect_.left + ((detailRect_.width - bounds.width) / 2.f) - bounds.left,
                      detailRect_.top + ((detailRect_.height - bounds.height) / 2.f) - bounds.top});
    target.draw(hint);
    return;
  }

  // The picture: a card-shaped placeholder for the deck's preview art (real
  // images arrive with the Sprint 10 cache). Keeps the real MTG 63:88 aspect,
  // fits the pane's width, and leaves room for the name + meta lines below.
  const float textBlock = (18.f + 13.f + 12.f) * scale_; // name + meta + gap
  const float picWidth = inner.width;
  const float maxPicHeight = inner.height - textBlock;
  const float picHeight = std::min(picWidth * kCardAspectH, maxPicHeight);
  const sf::FloatRect picture{inner.left, inner.top, picWidth, picHeight};
  sf::RectangleShape card({picture.width, picture.height});
  card.setPosition({picture.left, picture.top});
  card.setFillColor(menuPalette().panel);
  card.setOutlineThickness(2.f);
  card.setOutlineColor(menuPalette().gold);
  target.draw(card);
  // A subtle inner frame reads as a card's border.
  const sf::FloatRect innerFrame = inset(picture, 8.f * scale_);
  sf::RectangleShape frame({innerFrame.width, innerFrame.height});
  frame.setPosition({innerFrame.left, innerFrame.top});
  frame.setFillColor(sf::Color::Transparent);
  frame.setOutlineThickness(1.f);
  frame.setOutlineColor(menuPalette().muted);
  target.draw(frame);
  // The deck's initial stands in for the art until the cache lands.
  const std::string initial = deck->name.empty() ? "?" : std::string(1, deck->name.front());
  sf::Text monogram(initial, boldFont, static_cast<unsigned>(48.f * scale_));
  monogram.setFillColor(menuPalette().gold);
  const sf::FloatRect monoBounds = monogram.getLocalBounds();
  monogram.setPosition(
      {picture.left + ((picture.width - monoBounds.width) / 2.f) - monoBounds.left,
       picture.top + ((picture.height - monoBounds.height) / 2.f) - monoBounds.top});
  target.draw(monogram);

  // Name (bold) with the format / count meta under it, centered under the art.
  sf::Text name(deck->name, boldFont, static_cast<unsigned>(18.f * scale_));
  name.setFillColor(menuPalette().parchment);
  const sf::FloatRect nameBounds = name.getLocalBounds();
  name.setPosition({inner.left + ((inner.width - nameBounds.width) / 2.f) - nameBounds.left,
                    picture.top + picture.height + (8.f * scale_)});
  target.draw(name);
  const std::string meta = deck->format + " - " + std::to_string(deck->total_cards) + " cards (" +
                           std::to_string(deck->unique_cards) + " unique)";
  sf::Text metaText(meta, font, static_cast<unsigned>(13.f * scale_));
  metaText.setFillColor(menuPalette().muted);
  const sf::FloatRect metaBounds = metaText.getLocalBounds();
  metaText.setPosition({inner.left + ((inner.width - metaBounds.width) / 2.f) - metaBounds.left,
                        picture.top + picture.height + (8.f * scale_) + (18.f * scale_)});
  target.draw(metaText);
}

} // namespace mtgcpp::core
