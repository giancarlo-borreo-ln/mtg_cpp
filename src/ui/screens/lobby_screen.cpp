// Lobby screen implementation (M8.1).
#include "ui/screens/lobby_screen.h"

#include "ui/layout.h"
#include "ui/theme.h"

#include <SFML/Graphics/RectangleShape.hpp>
#include <SFML/Graphics/Text.hpp>

#include <algorithm>
#include <cctype>
#include <string>
#include <utility>

namespace mtgcpp::core {

namespace {

// Padding inside panels, in design pixels (scaled at use time).
constexpr float kPad = 20.f;
constexpr float kGap = 12.f;

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

// A framed panel fill.
void drawPanel(sf::RenderTarget &target, const sf::FloatRect &rect, sf::Color fill) {
  sf::RectangleShape panel({rect.width, rect.height});
  panel.setPosition({rect.left, rect.top});
  panel.setFillColor(fill);
  panel.setOutlineThickness(2.f);
  panel.setOutlineColor(menuPalette().gold);
  target.draw(panel);
}

} // namespace

LobbyScreen::LobbyScreen() {
  createButton_.setLabel("Create Room");
  joinButton_.setLabel("Join Room");
  leaveButton_.setLabel("Leave");
  joinInput_.setPlaceholder("127.0.0.1:7500");
}

void LobbyScreen::setConnected(bool connected) { connected_ = connected; }

void LobbyScreen::setConnecting(bool connecting) {
  connecting_ = connecting;
  createButton_.setEnabled(!connecting);
  joinButton_.setEnabled(!connecting);
  createButton_.setLabel(connecting ? "Creating..." : "Create Room");
}

void LobbyScreen::setRole(std::optional<PlayerSeat> role) { role_ = role; }

void LobbyScreen::setPlayerId(std::optional<std::string> playerId) {
  playerId_ = std::move(playerId);
}

void LobbyScreen::setPlayers(std::vector<std::string> players) { players_ = std::move(players); }

void LobbyScreen::setStatus(LobbyStatus status) { status_ = status; }

void LobbyScreen::setError(std::optional<std::string> error) { error_ = std::move(error); }

void LobbyScreen::setMyDeckName(std::optional<std::string> name) {
  myDeckName_ = std::move(name);
  deckIndex_.reset();
}

void LobbyScreen::setTheirDeckName(std::optional<std::string> name) {
  theirDeckName_ = std::move(name);
}

void LobbyScreen::setShareAddress(std::string address) { shareAddress_ = std::move(address); }

void LobbyScreen::setDecks(std::vector<DeckSummary> decks) {
  decks_ = std::move(decks);
  deckIndex_.reset();
  deckButtons_.clear();
  deckButtons_.reserve(decks_.size());
  for (const DeckSummary &deck : decks_) {
    Button button;
    button.setLabel(deck.name);
    button.setSubLabel(deck.format + " - " + std::to_string(deck.total_cards) + " cards");
    deckButtons_.push_back(std::move(button));
  }
}

void LobbyScreen::reset() {
  connected_ = false;
  connecting_ = false;
  setConnecting(false);
  role_.reset();
  playerId_.reset();
  players_.clear();
  status_ = LobbyStatus::Idle;
  error_.reset();
  myDeckName_.reset();
  theirDeckName_.reset();
  shareAddress_.clear();
  deckIndex_.reset();
  setDecks({});
}

bool LobbyScreen::canStartTable() const {
  return connected_ && status_ == LobbyStatus::Ready && myDeckName_.has_value() &&
         theirDeckName_.has_value();
}

void LobbyScreen::relayout(const sf::FloatRect &content, float scale) {
  scale_ = scale;
  content_ = content;
  if (!connected_) {
    relayoutConnect(content, scale);
  } else {
    relayoutRoom(content, scale);
  }
}

void LobbyScreen::relayoutConnect(const sf::FloatRect &content, float scale) {
  const float panelWidth = std::min(content.width, 380.f * scale);
  const float panelHeight = 230.f * scale;
  connectPanel_ = {content.left + ((content.width - panelWidth) / 2.f),
                   content.top + ((content.height - panelHeight) / 2.f), panelWidth, panelHeight};
  const sf::FloatRect inner = inset(connectPanel_, kPad * scale);

  // Create Room at the top of the panel.
  createButton_.setPosition({inner.left, inner.top});
  createButton_.setSize({inner.width, 44.f * scale});

  // "or" divider, then the join row at the bottom.
  const float dividerY = inner.top + (64.f * scale);
  const float joinTop = inner.top + (92.f * scale);
  const float joinHeight = 44.f * scale;
  const float joinButtonWidth = 110.f * scale;
  joinInput_.setPosition({inner.left, joinTop});
  joinInput_.setSize({inner.width - joinButtonWidth - (kGap * scale), joinHeight});
  joinButton_.setPosition({inner.left + inner.width - joinButtonWidth, joinTop});
  joinButton_.setSize({joinButtonWidth, joinHeight});
  (void)dividerY;
}

void LobbyScreen::relayoutRoom(const sf::FloatRect &content, float scale) {
  const sf::FloatRect inner = inset(content, kPad * scale);

  // Info lines at the top (share address for the host, seat, players).
  const float lineHeight = 22.f * scale;
  infoTop_ = {inner.left, inner.top, inner.width, 3.f * lineHeight};

  // The deck area: either the picker panel or the picked-deck status panel.
  const float deckTop = infoTop_.top + infoTop_.height + (kGap * scale);
  const float deckBottom = content.top + content.height - (kGap * scale) - (56.f * scale);
  const float deckHeight = std::max(deckBottom - deckTop, 0.f);
  const sf::FloatRect deckArea{inner.left, deckTop, inner.width, deckHeight};

  leaveButton_.setPosition({content.left + content.width - (140.f * scale),
                            content.top + content.height - (48.f * scale)});
  leaveButton_.setSize({140.f * scale, 44.f * scale});
  leaveRect_ = {content.left + content.width - (140.f * scale),
                content.top + content.height - (48.f * scale), 140.f * scale, 44.f * scale};

  hintRect_ = {inner.left, content.top + content.height - (112.f * scale), inner.width,
               22.f * scale};

  if (deckPickerVisible()) {
    deckPanel_ = deckArea;
    // "Choose your deck" heading, then one button per deck.
    const sf::FloatRect pickerInner = inset(deckPanel_, kPad * scale);
    const float headingTop = pickerInner.top;
    const float buttonTop = headingTop + (34.f * scale);
    const float rowHeight = 48.f * scale;
    for (std::size_t i = 0; i < deckButtons_.size(); ++i) {
      const float y = buttonTop + (static_cast<float>(i) * (rowHeight + (kGap * scale)));
      deckButtons_.at(i).setPosition({pickerInner.left, y});
      deckButtons_.at(i).setSize({pickerInner.width, rowHeight});
    }
  } else {
    statusPanel_ = deckArea;
  }
}

bool LobbyScreen::routeEvent(const sf::Event &event) {
  if (!connected_) {
    if (createButton_.handleEvent(event)) {
      return true;
    }
    if (joinInput_.handleEvent(event)) {
      return true;
    }
    if (joinButton_.handleEvent(event)) {
      return true;
    }
    return false;
  }
  if (leaveButton_.handleEvent(event)) {
    return true;
  }
  if (deckPickerVisible()) {
    for (Button &button : deckButtons_) {
      if (button.handleEvent(event)) {
        return true;
      }
    }
  }
  return false;
}

LobbyAction LobbyScreen::pollAction() {
  if (!connected_) {
    if (createButton_.consumeClicked()) {
      return LobbyAction::CreateRoom;
    }
    if (joinButton_.consumeClicked() || joinInput_.consumeSubmitted()) {
      // A join is ignored when the address is empty or all whitespace (the
      // webapp trims and uppercases its room code the same way).
      const std::string &address = joinInput_.text();
      const bool blank = std::all_of(address.begin(), address.end(),
                                     [](unsigned char c) { return std::isspace(c) != 0; });
      if (!blank) {
        return LobbyAction::JoinRoom;
      }
    }
    return LobbyAction::None;
  }
  if (leaveButton_.consumeClicked()) {
    return LobbyAction::LeaveRoom;
  }
  if (deckPickerVisible()) {
    std::size_t i = 0;
    for (Button &button : deckButtons_) {
      if (button.consumeClicked()) {
        deckIndex_ = i;
        return LobbyAction::ChooseDeck;
      }
      ++i;
    }
  }
  return LobbyAction::None;
}

void LobbyScreen::draw(sf::RenderTarget &target, const sf::Font &font,
                       const sf::Font &boldFont) const {
  const float textSize = 17.f * scale_;
  const float smallSize = 14.f * scale_;

  if (!connected_) {
    drawPanel(target, connectPanel_, menuPalette().panel);
    createButton_.draw(target, boldFont, scale_);
    drawText(target, font, "or", connectPanel_, textSize, menuPalette().muted, true);
    joinInput_.draw(target, font);
    joinButton_.draw(target, boldFont, scale_);
    if (error_.has_value()) {
      drawText(target, font, "Error: " + error_.value(), connectPanel_, smallSize,
               menuPalette().danger, true);
    }
    return;
  }

  // Info lines: the share address (host only), your seat and the players.
  std::string roleLabel = "guest";
  if (role_.has_value()) {
    roleLabel = role_.value() == PlayerSeat::Host ? "host" : "guest";
  }
  const std::string seatText =
      "You are " + roleLabel + (playerId_.has_value() ? " (" + playerId_.value() + ")" : "");
  std::string playersText = "Connected:";
  for (std::size_t i = 0; i < players_.size(); ++i) {
    playersText += (i == 0 ? " " : ", ") + players_.at(i);
  }

  sf::FloatRect line = infoTop_;
  const float lineHeight = 22.f * scale_;
  if (role_.has_value() && role_.value() == PlayerSeat::Host && !shareAddress_.empty()) {
    drawText(target, font, "Share this address: " + shareAddress_, line, smallSize,
             menuPalette().gold);
    line.top += lineHeight;
  }
  drawText(target, font, seatText, line, textSize, menuPalette().parchment);
  line.top += lineHeight;
  drawText(target, font, playersText, line, textSize, menuPalette().parchment);

  // Error under the info lines if any.
  if (error_.has_value()) {
    sf::FloatRect errorLine = line;
    errorLine.top += kGap * scale_;
    drawText(target, font, "Error: " + error_.value(), errorLine, smallSize, menuPalette().danger);
  }

  if (deckPickerVisible()) {
    drawPanel(target, deckPanel_, menuPalette().panel);
    drawText(target, boldFont, "Choose your deck", inset(deckPanel_, kPad * scale_), textSize,
             menuPalette().gold);
    for (const Button &button : deckButtons_) {
      button.draw(target, boldFont, scale_);
    }
  } else {
    drawPanel(target, statusPanel_, menuPalette().panel);
    const sf::FloatRect statusInner = inset(statusPanel_, kPad * scale_);
    drawText(target, font, "You're playing with " + myDeckName_.value_or("?"), statusInner,
             textSize, menuPalette().parchment);
    const std::string theirs = theirDeckName_.has_value() ? theirDeckName_.value() : "waiting...";
    sf::FloatRect theirLine = statusInner;
    theirLine.top += lineHeight;
    drawText(target, font, "Opponent's deck: " + theirs, theirLine, textSize, menuPalette().muted);
  }

  // The wait / ready hint.
  std::string hint;
  if (status_ != LobbyStatus::Ready) {
    hint = "Waiting for your opponent to join...";
  } else if (myDeckName_.has_value() && theirDeckName_.has_value()) {
    hint = "Both players ready. Starting the game...";
  } else {
    hint = "Both players connected - pick your decks to start.";
  }
  drawText(target, font, hint, hintRect_, smallSize, menuPalette().muted);

  leaveButton_.draw(target, boldFont, scale_);
}

} // namespace mtgcpp::core
