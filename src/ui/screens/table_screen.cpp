// Table screen implementation (M9.4 + M9.5): the Shandalar battlefield.
#include "ui/screens/table_screen.h"

#include "ui/layout.h"
#include "ui/theme.h"

#include <SFML/Graphics/RectangleShape.hpp>
#include <SFML/Graphics/Sprite.hpp>
#include <SFML/Graphics/Text.hpp>

#include <algorithm>
#include <cctype>
#include <string>
#include <utility>

namespace mtgcpp::core {

// Array-index helpers for the board enums (defined in state/board_state.h).
using state::seatIndex;
using state::zoneIndex;

namespace {

// Design-pixel proportions for the toolbar + ring.
constexpr float kToolbarHeight = 36.f;
constexpr float kPad = 10.f;
constexpr float kGap = 6.f;
constexpr float kLabelBand = 16.f; // room the zone label takes at the tile top
constexpr float kRingDiameter = 72.f;

// Draw one line of text, left-aligned inside `rect`, clipped to its width.
void drawText(sf::RenderTarget &target, const sf::Font &font, const std::string &text,
              const sf::FloatRect &rect, float size, sf::Color color, bool centered = false) {
  if (text.empty()) {
    return;
  }
  sf::Text label(text, font, static_cast<unsigned>(size));
  label.setFillColor(color);
  const sf::FloatRect bounds = label.getLocalBounds();
  if (centered) {
    label.setPosition({rect.left + ((rect.width - bounds.width) / 2.f) - bounds.left,
                       rect.top + ((rect.height - bounds.height) / 2.f) - bounds.top});
  } else {
    label.setPosition({rect.left - bounds.left, rect.top - bounds.top});
  }
  target.draw(label);
}

// A framed panel fill (used by the reveal prompt / revealed-hand panels).
void drawPanel(sf::RenderTarget &target, const sf::FloatRect &rect, sf::Color fill) {
  sf::RectangleShape panel({rect.width, rect.height});
  panel.setPosition({rect.left, rect.top});
  panel.setFillColor(fill);
  panel.setOutlineThickness(2.f);
  panel.setOutlineColor(tablePalette().gold);
  target.draw(panel);
}

} // namespace

TableScreen::TableScreen() {
  revealButton_.setLabel("Request Hand Reveal");
  leaveButton_.setLabel("Leave");
  acceptButton_.setLabel("Accept");
  denyButton_.setLabel("Deny");
  dismissButton_.setLabel("Not now");
  closeRevealedButton_.setLabel("Close");
}

void TableScreen::setBoard(const state::BoardState &board) { board_ = board; }

void TableScreen::setRole(std::optional<PlayerSeat> role) { role_ = role; }

void TableScreen::setPlayerId(std::optional<std::string> playerId) {
  playerId_ = std::move(playerId);
}

void TableScreen::setRevealRequest(std::optional<std::string> from) {
  revealRequestFrom_ = std::move(from);
}

void TableScreen::setRevealResult(std::optional<bool> accepted, std::vector<RevealCard> hand) {
  revealAccepted_ = accepted;
  revealedHand_ = std::move(hand);
}

void TableScreen::setError(std::optional<std::string> error) { error_ = std::move(error); }

std::vector<RevealCard> TableScreen::revealCards() const {
  return toRevealCards(board_.seats.at(seatIndex(mySeat())).hand);
}

void TableScreen::relayout(const sf::FloatRect &content, float scale) {
  scale_ = scale;
  content_ = content;

  // Build the procedural textures once (they are resolution-independent; the
  // sprites scale them to each slot).
  if (frontTex_.getSize().x == 0u) {
    buildCardFrontTexture(frontTex_, {240u, 335u});
    buildCardBackTexture(backTex_, {240u, 335u});
    buildTokenTexture(tokenTex_, {240u, 335u});
    buildZoneTileTexture(tileTex_, 128u);
    buildLifeRingTexture(ringTex_, 128u);
  }

  const PlayerSeat mine = mySeat();
  const PlayerSeat theirs = theirSeat();
  const float gap = kGap * scale;

  // Toolbar strip at the top (seat info + reveal/leave buttons), then the
  // battlefield bands below it.
  toolbar_ = {content.left, content.top, content.width, kToolbarHeight * scale};
  battlefield_ = {content.left, toolbar_.top + toolbar_.height + gap, content.width,
                  content.height - toolbar_.height - gap};
  const TableBands bands = tableBands(battlefield_, gap, 0.44f);

  // Hands: the opponent's strip is the top band (drawn face-down), yours the
  // bottom (arched). The arch amplitude lifts the opponent's row toward the
  // middle and dips yours.
  hand_rects_.at(seatIndex(mine)) = bands.myHand;
  hand_rects_.at(seatIndex(theirs)) = bands.theirHand;

  // Zone grids for both halves: same geometry, different piles.
  for (const PlayerSeat seat : {PlayerSeat::Host, PlayerSeat::Guest}) {
    const sf::FloatRect grid = seat == theirs ? bands.theirGrid : bands.myGrid;
    for (const PlayerZone zone : kPlayerZones) {
      const std::size_t zi = zoneIndex(zone);
      ZoneLayout &slot = zones_.at(seatIndex(seat)).at(zi);
      slot.zone_rect = tableZoneRect(grid, zone);
      // Cards sit below the label band so they never overlap the zone name.
      sf::FloatRect cardArea = slot.zone_rect;
      cardArea.top += kLabelBand * scale;
      cardArea.height -= kLabelBand * scale;
      slot.card_rects =
          zoneCardRects(cardArea, board_.seats.at(seatIndex(seat)).zones.at(zi).size()).cards;
    }
  }

  // The Stack: a single card sized to the stack band, then fanned in play order
  // by the fixed stack offset (the ONLY overlap on the table).
  const float stackCardHeight = std::min(bands.stack.height, bands.stack.width / kCardAspect);
  const float stackCardWidth = stackCardHeight * kCardAspect;
  stack_card_size_ = {stackCardWidth, stackCardHeight};
  stack_base_ = {bands.stack.left + ((bands.stack.width - stackCardWidth) / 2.f),
                 bands.stack.top + ((bands.stack.height - stackCardHeight) / 2.f), stackCardWidth,
                 stackCardHeight};
  stack_rects_ = stackCardRects(stack_base_, board_.stack.size());

  // Life rings at the outer corners: opponent top-left, you bottom-right.
  const float ring = kRingDiameter * scale;
  const float pad = kPad * scale;
  life_rings_.at(seatIndex(theirs)) = {battlefield_.left + pad, battlefield_.top + pad, ring, ring};
  life_rings_.at(seatIndex(mine)) = {battlefield_.left + battlefield_.width - ring - pad,
                                     battlefield_.top + battlefield_.height - ring - pad, ring,
                                     ring};

  // Toolbar buttons: reveal (with the leave button to its right).
  const float buttonHeight = kToolbarHeight * scale;
  const float leaveWidth = 90.f * scale;
  const float revealWidth = 190.f * scale;
  leaveButton_.setPosition(
      {toolbar_.left + toolbar_.width - leaveWidth - (gap * scale), toolbar_.top});
  leaveButton_.setSize({leaveWidth, buttonHeight});
  revealButton_.setPosition(
      {leaveButton_.position().x - revealWidth - (gap * scale), toolbar_.top});
  revealButton_.setSize({revealWidth, buttonHeight});

  // Reveal consent panel (centered) with a button row.
  const float panelW = 420.f * scale;
  const float panelH = 150.f * scale;
  revealPromptRect_ = {battlefield_.left + ((battlefield_.width - panelW) / 2.f),
                       battlefield_.top + ((battlefield_.height - panelH) / 2.f), panelW, panelH};
  const float buttonW = 100.f * scale;
  const float buttonY = revealPromptRect_.top + revealPromptRect_.height - (44.f * scale);
  acceptButton_.setPosition({revealPromptRect_.left + (kPad * scale), buttonY});
  acceptButton_.setSize({buttonW, buttonHeight});
  denyButton_.setPosition({acceptButton_.position().x + buttonW + (gap * scale), buttonY});
  denyButton_.setSize({buttonW, buttonHeight});
  dismissButton_.setPosition({denyButton_.position().x + buttonW + (gap * scale), buttonY});
  dismissButton_.setSize({buttonW, buttonHeight});

  // Revealed-hand panel on the right edge.
  revealedPanelRect_ = {battlefield_.left + battlefield_.width - (320.f * scale),
                        battlefield_.top + (60.f * scale), 300.f * scale, 260.f * scale};
  closeRevealedButton_.setPosition(
      {revealedPanelRect_.left + revealedPanelRect_.width - (90.f * scale) - (kPad * scale),
       revealedPanelRect_.top + (kPad * scale)});
  closeRevealedButton_.setSize({90.f * scale, buttonHeight});
}

const std::vector<BoardCard> &TableScreen::pileFor(PlayerSeat seat, bool in_hand,
                                                   std::optional<PlayerZone> zone,
                                                   bool in_stack) const {
  if (in_stack) {
    return board_.stack;
  }
  if (in_hand) {
    return board_.seats.at(seatIndex(seat)).hand;
  }
  const PlayerZone z = zone.value_or(PlayerZone::Creatures);
  return board_.seats.at(seatIndex(seat)).zones.at(zoneIndex(z));
}

std::optional<CardSelection> TableScreen::cardAt(sf::Vector2f point) const {
  // Walk the piles top-most first (my hand, my zones, the Stack, their zones,
  // their hand) so an overlap always resolves to the card drawn on top.
  const PlayerSeat mine = mySeat();
  const PlayerSeat theirs = theirSeat();
  // The hand strip is one band; the per-card rects arch within it (drawn the
  // same way in drawHand). The opponent's row arches up, yours dips.
  auto handCards = [this](PlayerSeat seat, std::size_t count) {
    const bool archUp = seat == theirSeat();
    const float amplitude = hand_rects_.at(seatIndex(seat)).height * 0.12f;
    return handCardRects(hand_rects_.at(seatIndex(seat)), count, amplitude, archUp);
  };
  if (const std::optional<std::size_t> i =
          indexAtPoint(handCards(mine, board_.seats.at(seatIndex(mine)).hand.size()), point)) {
    return CardSelection{
        mine,         /*in_hand=*/true, /*in_stack=*/false,
        std::nullopt, i.value(),        board_.seats.at(seatIndex(mine)).hand.at(i.value())};
  }
  for (std::size_t zi = kPlayerZoneCount; zi > 0; --zi) {
    const std::size_t idx = zi - 1;
    if (const std::optional<std::size_t> i =
            indexAtPoint(zones_.at(seatIndex(mine)).at(idx).card_rects, point)) {
      return CardSelection{mine,
                           /*in_hand=*/false,
                           /*in_stack=*/false,
                           kPlayerZones.at(idx),
                           i.value(),
                           board_.seats.at(seatIndex(mine)).zones.at(idx).at(i.value())};
    }
  }
  if (const std::optional<std::size_t> i = indexAtPoint(stack_rects_, point)) {
    return CardSelection{PlayerSeat::Host, /*in_hand=*/false, /*in_stack=*/true,
                         std::nullopt,     i.value(),         board_.stack.at(i.value())};
  }
  for (std::size_t zi = kPlayerZoneCount; zi > 0; --zi) {
    const std::size_t idx = zi - 1;
    if (const std::optional<std::size_t> i =
            indexAtPoint(zones_.at(seatIndex(theirs)).at(idx).card_rects, point)) {
      return CardSelection{theirs,
                           /*in_hand=*/false,
                           /*in_stack=*/false,
                           kPlayerZones.at(idx),
                           i.value(),
                           board_.seats.at(seatIndex(theirs)).zones.at(idx).at(i.value())};
    }
  }
  if (const std::optional<std::size_t> i =
          indexAtPoint(handCards(theirs, board_.seats.at(seatIndex(theirs)).hand.size()), point)) {
    return CardSelection{
        theirs,       /*in_hand=*/true, /*in_stack=*/false,
        std::nullopt, i.value(),        board_.seats.at(seatIndex(theirs)).hand.at(i.value())};
  }
  return std::nullopt;
}

void TableScreen::closeMenu() {
  menuOpen_ = false;
  moveMenu_ = false;
  menu_.clear();
}

std::optional<sf::FloatRect> TableScreen::selectedCardRect() const {
  if (!selection_.has_value() || selection_->seat != mySeat() || selection_->in_stack) {
    return std::nullopt;
  }
  if (selection_->in_hand) {
    const bool archUp = selection_->seat == theirSeat();
    const sf::FloatRect &strip = hand_rects_.at(seatIndex(selection_->seat));
    const float amplitude = strip.height * 0.12f;
    const std::vector<sf::FloatRect> rects = handCardRects(
        strip, board_.seats.at(seatIndex(selection_->seat)).hand.size(), amplitude, archUp);
    if (selection_->index < rects.size()) {
      return rects.at(selection_->index);
    }
    return std::nullopt;
  }
  const std::size_t zi = zoneIndex(selection_->zone.value_or(PlayerZone::Creatures));
  const std::vector<sf::FloatRect> &rects =
      zones_.at(seatIndex(selection_->seat)).at(zi).card_rects;
  if (selection_->index < rects.size()) {
    return rects.at(selection_->index);
  }
  return std::nullopt;
}

void TableScreen::rebuildMenu() {
  if (!selection_.has_value() || selection_->in_stack || selection_->seat != mySeat()) {
    return;
  }
  if (moveMenu_) {
    menu_.setItems({"Move to Lands", "Move to Creatures", "Move to Instants / Sorceries",
                    "Move to Graveyard", "Move to Exile", "Move to Stack"});
    return;
  }
  const std::string tap = selection_->card.tapped ? "Untap" : "Tap";
  const std::string flip = selection_->card.flipped ? "Flip to front" : "Flip to back";
  menu_.setItems({tap, "+1/+1 Counter", "Create Token", flip, "Move..."});
}

void TableScreen::moveSelection(PlayerZone zone, bool toStack) {
  if (!selection_.has_value() || selection_->seat != mySeat()) {
    return;
  }
  if (toStack) {
    action_ = state::moveCardToStack(selection_->card.id);
  } else {
    action_ = state::moveCardToZone(selection_->seat, selection_->card.id, zone);
  }
  queuedAction_ = TableAction::CardCommand;
  selection_.reset();
}

void TableScreen::runCommand(std::size_t menuIndex) {
  if (!selection_.has_value() || selection_->seat != mySeat() || selection_->in_stack) {
    closeMenu();
    return;
  }
  if (moveMenu_) {
    if (menuIndex < kPlayerZoneCount) {
      moveSelection(kPlayerZones.at(menuIndex), /*toStack=*/false);
    } else if (menuIndex == kPlayerZoneCount) {
      moveSelection(PlayerZone::Creatures, /*toStack=*/true);
    }
    closeMenu();
    return;
  }
  const PlayerSeat seat = selection_->seat;
  const std::string id = selection_->card.id;
  switch (menuIndex) {
  case 0:
    action_ = state::tapCard(seat, id);
    queuedAction_ = TableAction::CardCommand;
    break;
  case 1:
    action_ = state::addCounter(seat, id);
    queuedAction_ = TableAction::CardCommand;
    break;
  case 2: {
    // A token appears in the zone the card is in (creatures for hand cards).
    const PlayerZone zone = selection_->zone.value_or(PlayerZone::Creatures);
    action_ = state::createToken(seat, zone, selection_->card.name);
    queuedAction_ = TableAction::CardCommand;
    break;
  }
  case 3:
    action_ = state::flipCard(seat, id);
    queuedAction_ = TableAction::CardCommand;
    break;
  case 4:
    moveMenu_ = true;
    rebuildMenu();
    return; // the menu stays open for the target list
  default:
    closeMenu();
    return;
  }
  closeMenu();
}

void TableScreen::beginLifeEdit(PlayerSeat seat) {
  lifeEditingSeat_ = seat;
  lifeEditOriginal_ = std::to_string(board_.life.at(seatIndex(seat)));
  lifeEditText_ = lifeEditOriginal_;
}

void TableScreen::commitLifeEdit() {
  if (!lifeEditingSeat_.has_value()) {
    return;
  }
  action_ = state::setLife(lifeEditingSeat_.value(), parseLife(lifeEditText_));
  queuedAction_ = TableAction::CardCommand;
  lifeEditingSeat_.reset();
  lifeEditText_.clear();
}

void TableScreen::cancelLifeEdit() {
  lifeEditingSeat_.reset();
  lifeEditText_.clear();
}

bool TableScreen::mousePressed(sf::Vector2f point) {
  // A life ring click starts editing its number (both rings are editable).
  for (const PlayerSeat seat : {PlayerSeat::Host, PlayerSeat::Guest}) {
    if (life_rings_.at(seatIndex(seat)).contains(point)) {
      beginLifeEdit(seat);
      return true;
    }
  }
  // A click outside the open menu closes it (and falls through to select).
  if (menuOpen_) {
    if (menu_.mousePressed(point)) {
      return true;
    }
    closeMenu();
  }
  const std::optional<CardSelection> hit = cardAt(point);
  if (hit.has_value() && hit->seat == mySeat() && !hit->in_stack) {
    selection_ = hit;
  } else {
    selection_.reset();
  }
  return true;
}

bool TableScreen::mouseReleased(sf::Vector2f point) {
  if (menuOpen_) {
    return menu_.mouseReleased(point);
  }
  return false;
}

bool TableScreen::mousePressedRight(sf::Vector2f point) {
  // Right-click opens the command menu on a card — only for your own cards.
  const std::optional<CardSelection> hit = cardAt(point);
  if (!hit.has_value() || hit->seat != mySeat() || hit->in_stack) {
    closeMenu();
    return true;
  }
  selection_ = hit;
  moveMenu_ = false;
  rebuildMenu();
  menuPosition_ = point;
  menu_.setPosition(point);
  menuOpen_ = true;
  return true;
}

bool TableScreen::keyPressed(sf::Event::KeyEvent key) {
  if (lifeEditingSeat_.has_value()) {
    switch (key.code) {
    case sf::Keyboard::Escape:
      cancelLifeEdit();
      return true;
    case sf::Keyboard::Enter:
      commitLifeEdit();
      return true;
    case sf::Keyboard::Backspace:
      if (!lifeEditText_.empty()) {
        lifeEditText_.pop_back();
      }
      return true;
    case sf::Keyboard::Num0:
    case sf::Keyboard::Num1:
    case sf::Keyboard::Num2:
    case sf::Keyboard::Num3:
    case sf::Keyboard::Num4:
    case sf::Keyboard::Num5:
    case sf::Keyboard::Num6:
    case sf::Keyboard::Num7:
    case sf::Keyboard::Num8:
    case sf::Keyboard::Num9:
      if (lifeEditText_.size() < 4) {
        // The first digit replaces the prefilled current value.
        const char digit = static_cast<char>('0' + (key.code - sf::Keyboard::Num0));
        if (lifeEditText_ == lifeEditOriginal_) {
          lifeEditText_ = std::string(1, digit);
        } else {
          lifeEditText_.push_back(digit);
        }
      }
      return true;
    default:
      return false;
    }
  }
  if (key.code == sf::Keyboard::Escape) {
    if (menuOpen_) {
      closeMenu();
      return true;
    }
    if (selection_.has_value()) {
      selection_.reset();
      return true;
    }
    return false; // nothing to clear — the App may quit
  }
  if (!selection_.has_value() || selection_->seat != mySeat() || selection_->in_stack) {
    return false; // nothing to command — let the App keep routing keys
  }
  // Move-target mode: number keys pick the destination zone (1-5) or the
  // Stack (6).
  if (menuOpen_ && moveMenu_) {
    switch (key.code) {
    case sf::Keyboard::Num1:
      moveSelection(kPlayerZones.at(0), false);
      closeMenu();
      return true;
    case sf::Keyboard::Num2:
      moveSelection(kPlayerZones.at(1), false);
      closeMenu();
      return true;
    case sf::Keyboard::Num3:
      moveSelection(kPlayerZones.at(2), false);
      closeMenu();
      return true;
    case sf::Keyboard::Num4:
      moveSelection(kPlayerZones.at(3), false);
      closeMenu();
      return true;
    case sf::Keyboard::Num5:
      moveSelection(kPlayerZones.at(4), false);
      closeMenu();
      return true;
    case sf::Keyboard::Num6:
      moveSelection(PlayerZone::Creatures, true);
      closeMenu();
      return true;
    default:
      return false;
    }
  }
  switch (key.code) {
  case sf::Keyboard::T:
    runCommand(0); // Tap / Untap
    return true;
  case sf::Keyboard::C:
    runCommand(1); // +1/+1 Counter
    return true;
  case sf::Keyboard::K:
    runCommand(2); // Create Token
    return true;
  case sf::Keyboard::F:
    runCommand(3); // Flip
    return true;
  case sf::Keyboard::M:
    moveMenu_ = true;
    rebuildMenu();
    if (const std::optional<sf::FloatRect> slot = selectedCardRect(); slot.has_value()) {
      menuPosition_ = {slot->left + slot->width, slot->top};
    }
    menu_.setPosition(menuPosition_);
    menuOpen_ = true;
    return true;
  case sf::Keyboard::S:
    moveSelection(PlayerZone::Creatures, true); // To Stack
    closeMenu();
    return true;
  default:
    return false;
  }
}

bool TableScreen::routeEvent(const sf::Event &event) {
  // Toolbar / reveal / revealed-panel buttons get first pick for both mouse
  // buttons; the table's own click logic handles everything else.
  const bool promptVisible = revealRequestFrom_.has_value();
  const bool panelVisible = revealAccepted_.has_value();

  if (event.type == sf::Event::MouseMoved) {
    menu_.setMousePosition(
        {static_cast<float>(event.mouseMove.x), static_cast<float>(event.mouseMove.y)});
    return false;
  }
  if (event.type == sf::Event::MouseButtonPressed) {
    const sf::Vector2f point{static_cast<float>(event.mouseButton.x),
                             static_cast<float>(event.mouseButton.y)};
    if (event.mouseButton.button == sf::Mouse::Right) {
      return mousePressedRight(point);
    }
    if (event.mouseButton.button != sf::Mouse::Left) {
      return false;
    }
    if (promptVisible) {
      if (acceptButton_.handleEvent(event)) {
        return true;
      }
      if (denyButton_.handleEvent(event)) {
        return true;
      }
      if (dismissButton_.handleEvent(event)) {
        return true;
      }
    }
    if (panelVisible && closeRevealedButton_.handleEvent(event)) {
      return true;
    }
    if (revealButton_.handleEvent(event)) {
      return true;
    }
    if (leaveButton_.handleEvent(event)) {
      return true;
    }
    return mousePressed(point);
  }
  if (event.type == sf::Event::MouseButtonReleased) {
    if (event.mouseButton.button != sf::Mouse::Left) {
      return false;
    }
    const sf::Vector2f point{static_cast<float>(event.mouseButton.x),
                             static_cast<float>(event.mouseButton.y)};
    if (promptVisible) {
      if (acceptButton_.handleEvent(event)) {
        return true;
      }
      if (denyButton_.handleEvent(event)) {
        return true;
      }
      if (dismissButton_.handleEvent(event)) {
        return true;
      }
    }
    if (panelVisible && closeRevealedButton_.handleEvent(event)) {
      return true;
    }
    if (revealButton_.handleEvent(event)) {
      return true;
    }
    if (leaveButton_.handleEvent(event)) {
      return true;
    }
    return mouseReleased(point);
  }
  if (event.type == sf::Event::KeyPressed) {
    return keyPressed(event.key);
  }
  return false;
}

TableAction TableScreen::pollAction() {
  // The reveal/toolbar buttons resolve on release; read them first.
  if (revealButton_.consumeClicked()) {
    return TableAction::RequestReveal;
  }
  if (leaveButton_.consumeClicked()) {
    return TableAction::Leave;
  }
  if (revealRequestFrom_.has_value()) {
    if (acceptButton_.consumeClicked()) {
      return TableAction::AcceptReveal;
    }
    if (denyButton_.consumeClicked()) {
      return TableAction::DenyReveal;
    }
    if (dismissButton_.consumeClicked()) {
      return TableAction::DismissReveal;
    }
  }
  if (revealAccepted_.has_value() && closeRevealedButton_.consumeClicked()) {
    return TableAction::ClearRevealed;
  }
  if (menuOpen_) {
    if (const std::optional<std::size_t> index = menu_.consumeSelection(); index.has_value()) {
      runCommand(index.value());
    }
  }
  // A queued command (from a menu selection or a keyboard verb) is delivered
  // once per poll.
  if (queuedAction_.has_value()) {
    const TableAction action = queuedAction_.value();
    queuedAction_.reset();
    return action;
  }
  return TableAction::None;
}

void TableScreen::draw(sf::RenderTarget &target, const sf::Font &font,
                       const sf::Font &boldFont) const {
  const PlayerSeat mine = mySeat();
  const PlayerSeat theirs = theirSeat();
  const float scale = scale_;

  // Toolbar: seat identity on the left, reveal/leave buttons on the right.
  const float lineHeight = kToolbarHeight * scale;
  std::string seatText = "You are " + playerSeatToString(mine);
  if (playerId_.has_value()) {
    seatText += " (" + playerId_.value() + ")";
  }
  drawText(target, boldFont, seatText,
           {toolbar_.left + (kPad * scale), toolbar_.top, toolbar_.width, lineHeight},
           lineHeight * 0.42f, tablePalette().gold);
  std::string selectedText;
  if (selection_.has_value() && selection_->seat == mine && !selection_->in_stack) {
    selectedText = "Selected: " + selection_->card.name;
    if (!selection_->card.mana_cost.empty()) {
      selectedText += " " + selection_->card.mana_cost;
    }
  } else if (lifeEditingSeat_.has_value()) {
    selectedText = "Life: " + lifeEditText_ + "_";
  }
  sf::FloatRect selectedLine = toolbar_;
  selectedLine.top += lineHeight * 0.55f;
  drawText(target, font, selectedText, selectedLine, lineHeight * 0.32f, tablePalette().parchment);
  revealButton_.draw(target, boldFont, scale_);
  leaveButton_.draw(target, boldFont, scale_);

  // --- Opponent half (top, read-only) -------------------------------------
  drawHand(target, font, theirs);
  drawZones(target, font, theirs);
  drawStack(target, font);
  // --- Your half (bottom, interactive) ------------------------------------
  drawZones(target, font, mine);
  drawHand(target, font, mine);

  // Selection highlight: a gold frame around the selected card's slot.
  if (const std::optional<sf::FloatRect> slot = selectedCardRect(); slot.has_value()) {
    sf::RectangleShape frame({slot->width, slot->height});
    frame.setPosition({slot->left, slot->top});
    frame.setFillColor(sf::Color::Transparent);
    frame.setOutlineThickness(2.f * scale);
    frame.setOutlineColor(tablePalette().gold);
    target.draw(frame);
  }

  // Life rings at the corners (both editable).
  drawLifeRing(target, font, theirs);
  drawLifeRing(target, font, mine);

  // The command menu (drawn last so it floats above everything).
  if (menuOpen_) {
    menu_.draw(target, font, scale_);
  }

  if (error_.has_value()) {
    drawText(target, font, "Error: " + error_.value(),
             {battlefield_.left + (kPad * scale),
              battlefield_.top + battlefield_.height - (20.f * scale), battlefield_.width,
              20.f * scale},
             12.f * scale, menuPalette().danger);
  }

  // Reveal consent prompt.
  if (revealRequestFrom_.has_value()) {
    drawPanel(target, revealPromptRect_, menuPalette().panel);
    const std::string message = revealRequestFrom_.value() + " wants to see your hand.";
    drawText(target, font, message, inset(revealPromptRect_, kPad * scale), 15.f * scale,
             menuPalette().parchment);
    drawText(target, font, "Accepting sends its card images to them.",
             {revealPromptRect_.left + (kPad * scale), revealPromptRect_.top + (34.f * scale),
              revealPromptRect_.width - (2.f * kPad * scale), 18.f * scale},
             12.f * scale, menuPalette().muted);
    acceptButton_.draw(target, boldFont, scale_);
    denyButton_.draw(target, boldFont, scale_);
    dismissButton_.draw(target, boldFont, scale_);
  }

  // Revealed-hand panel (read-only).
  if (revealAccepted_.has_value()) {
    drawPanel(target, revealedPanelRect_, menuPalette().panel);
    const std::string title = revealAccepted_.value() ? "Hand revealed" : "Hand reveal denied";
    drawText(target, boldFont, title,
             {revealedPanelRect_.left + (kPad * scale), revealedPanelRect_.top + (kPad * scale),
              revealedPanelRect_.width, 20.f * scale},
             15.f * scale, tablePalette().gold);
    closeRevealedButton_.draw(target, boldFont, scale_);
    if (revealAccepted_.value() && !revealedHand_.empty()) {
      float y = revealedPanelRect_.top + (48.f * scale);
      for (const RevealCard &card : revealedHand_) {
        drawText(
            target, font, card.name,
            {revealedPanelRect_.left + (kPad * scale), y, revealedPanelRect_.width, 18.f * scale},
            13.f * scale, menuPalette().parchment);
        y += 22.f * scale;
        if (y > revealedPanelRect_.top + revealedPanelRect_.height - (24.f * scale)) {
          break;
        }
      }
    }
  }
}

void TableScreen::drawHand(sf::RenderTarget &target, const sf::Font &font, PlayerSeat seat) const {
  // The opponent's hand is rendered face-down (you cannot see it); your hand
  // shows the card faces. The cards arch within their strip.
  const std::vector<BoardCard> &hand = board_.seats.at(seatIndex(seat)).hand;
  const bool archUp = seat == theirSeat();
  const float amplitude = hand_rects_.at(seatIndex(seat)).height * 0.12f;
  const std::vector<sf::FloatRect> rects =
      handCardRects(hand_rects_.at(seatIndex(seat)), hand.size(), amplitude, archUp);
  for (std::size_t i = 0; i < hand.size(); ++i) {
    if (seat == mySeat()) {
      CardView view;
      view.setCard(hand.at(i));
      view.setPosition({rects.at(i).left, rects.at(i).top});
      view.setSize({rects.at(i).width, rects.at(i).height});
      view.draw(target, font, frontTex_, backTex_, tokenTex_);
    } else {
      drawFace(target, rects.at(i), backTex_);
    }
  }
}

void TableScreen::drawZones(sf::RenderTarget &target, const sf::Font &font, PlayerSeat seat) const {
  for (const PlayerZone zone : kPlayerZones) {
    const std::size_t zi = zoneIndex(zone);
    const ZoneLayout &slot = zones_.at(seatIndex(seat)).at(zi);
    // The zone tile + label (ZoneView), then the cards laid on top.
    ZoneView view;
    view.setPosition({slot.zone_rect.left, slot.zone_rect.top});
    view.setSize({slot.zone_rect.width, slot.zone_rect.height});
    view.setLabel(zoneLabel(zone));
    view.setEmpty(board_.seats.at(seatIndex(seat)).zones.at(zi).empty());
    view.draw(target, font, tileTex_);
    const std::vector<BoardCard> &cards = board_.seats.at(seatIndex(seat)).zones.at(zi);
    for (std::size_t i = 0; i < cards.size(); ++i) {
      CardView card;
      card.setCard(cards.at(i));
      card.setPosition({slot.card_rects.at(i).left, slot.card_rects.at(i).top});
      card.setSize({slot.card_rects.at(i).width, slot.card_rects.at(i).height});
      card.draw(target, font, frontTex_, backTex_, tokenTex_);
    }
  }
}

void TableScreen::drawStack(sf::RenderTarget &target, const sf::Font &font) const {
  const std::vector<sf::FloatRect> &rects = stack_rects_;
  for (std::size_t i = 0; i < board_.stack.size(); ++i) {
    if (i >= rects.size()) {
      break;
    }
    drawFace(target, rects.at(i), backTex_);
  }
  if (board_.stack.empty()) {
    drawText(target, font, "The Stack", stack_base_, 11.f * scale_, tablePalette().goldDim, true);
  }
}

void TableScreen::drawFace(sf::RenderTarget &target, const sf::FloatRect &rect,
                           const sf::Texture &texture) {
  if (rect.width <= 0.f || rect.height <= 0.f || texture.getSize().x == 0u) {
    return;
  }
  sf::Sprite sprite(texture);
  sprite.setScale({rect.width / static_cast<float>(texture.getSize().x),
                   rect.height / static_cast<float>(texture.getSize().y)});
  sprite.setPosition({rect.left, rect.top});
  target.draw(sprite);
}

void TableScreen::drawLifeRing(sf::RenderTarget &target, const sf::Font &font,
                               PlayerSeat seat) const {
  const sf::FloatRect &ring = life_rings_.at(seatIndex(seat));
  if (ring.width <= 0.f) {
    return;
  }
  sf::Sprite sprite(ringTex_);
  sprite.setScale({ring.width / static_cast<float>(ringTex_.getSize().x),
                   ring.height / static_cast<float>(ringTex_.getSize().y)});
  sprite.setPosition({ring.left, ring.top});
  target.draw(sprite);
  const int life = board_.life.at(seatIndex(seat));
  const bool editing = lifeEditingSeat_.has_value() && lifeEditingSeat_.value() == seat;
  const std::string text = editing ? lifeEditText_ : std::to_string(life);
  const float size = ring.width * 0.42f;
  sf::Text label(text, font, static_cast<unsigned>(size));
  label.setFillColor(editing ? tablePalette().gold : tablePalette().parchment);
  const sf::FloatRect bounds = label.getLocalBounds();
  label.setPosition({ring.left + ((ring.width - bounds.width) / 2.f) - bounds.left,
                     ring.top + ((ring.height - bounds.height) / 2.f) - bounds.top});
  target.draw(label);
}

} // namespace mtgcpp::core
