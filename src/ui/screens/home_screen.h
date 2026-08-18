// The Home screen (M4.3): pick who you are, then manage your saved decks.
//
// Mirrors the webapp's home.component.html. With no active profile a
// "Choose your player" grid of the four fixed profiles is shown; once one is
// picked, the screen becomes a "Deck Vault" listing the saved decks with a
// per-row Delete button, an empty state, and the Play Online / + New Deck /
// Switch player actions.
//
// The screen owns all of its widgets but ZERO I/O: the App drives the deck
// repository and the profile store, and translates the actions this screen
// reports (pollAction) into file/network work. Everything except draw() is
// pure and headless-testable. relayout() recomputes every widget rect from the
// current content band, so the screen re-flows on window resize — the profile
// grid drops from 4 columns to 2 to 1 as the window narrows (ui/layout.h).
#pragma once

#include "core/card.h"
#include "ui/widgets/button.h"
#include "ui/widgets/deck_list_view.h"

#include <SFML/Graphics/Font.hpp>
#include <SFML/Graphics/Rect.hpp>
#include <SFML/Graphics/RenderTarget.hpp>
#include <SFML/System/Vector2.hpp>
#include <SFML/Window/Event.hpp>

#include <array>
#include <cstddef>
#include <optional>
#include <string>

namespace mtgcpp::core {

// What the user just did on Home. The App reacts by touching the repository /
// profile store / router; the screen itself never does I/O.
enum class HomeAction {
  None,
  SelectProfile, // playerId() holds the profile just picked
  DeleteDeck,    // deleteIndex() holds the deck to remove
  SwitchPlayer,
  NewDeck,
  EditDeck, // opens the highlighted deck in the Deck Editor
  PlayOnline,
};

class HomeScreen {
public:
  HomeScreen();

  // --- View state -----------------------------------------------------------
  // The active profile, or nullopt while the picker is shown. Setting a value
  // flips the view to the vault; clearing it flips back to the picker.
  const std::optional<std::string> &playerId() const { return playerId_; }
  void setPlayerId(std::optional<std::string> playerId);
  bool isProfilePicker() const { return !playerId_.has_value(); }

  // Deck vault data (replaces the list; clears selection + pending delete).
  void setDecks(std::vector<DeckSummary> decks);
  const std::vector<DeckSummary> &decks() const { return deckList_.decks(); }
  bool hasDecks() const { return !deckList_.empty(); }

  // The deck the user is highlighting (selection), or nullptr when no deck is
  // selected. The vault's detail pane shows this deck's name and picture.
  const DeckSummary *highlightedDeck() const;

  // The index queued for deletion by a Delete action, read by the App before it
  // removes the deck (stale by the next setDecks, which clears it).
  std::optional<std::size_t> deleteIndex() const { return deleteIndex_; }

  // --- Layout ---------------------------------------------------------------
  // Recompute every widget rect from the content band (`content`) and the
  // responsive UI scale. Called at startup and again on every window resize.
  void relayout(const sf::FloatRect &content, float scale);

  // --- Interaction -------------------------------------------------------------
  // Forward an SFML event to the widgets of the active view. Returns true when
  // a widget consumed it (the app then must not route a screen switch).
  bool routeEvent(const sf::Event &event);
  // Drain one completed action (clicks resolve on release), or None. Call once
  // per frame; the app reacts to anything != None.
  HomeAction pollAction();

  void draw(sf::RenderTarget &target, const sf::Font &font, const sf::Font &boldFont) const;

  // Exposed for tests: the picker's profile cards (index = profile order) and
  // the vault's widgets.
  const Button &profileButton(std::size_t index) const { return profileButtons_.at(index); }
  const DeckListView &deckList() const { return deckList_; }
  const Button &playButton() const { return playButton_; }
  const Button &newDeckButton() const { return newDeckButton_; }
  const Button &editButton() const { return editButton_; }
  const Button &switchButton() const { return switchButton_; }

private:
  void relayoutPicker(const sf::FloatRect &content, float scale);
  void relayoutVault(const sf::FloatRect &content, float scale);
  void setAction(HomeAction action);
  // The vault's right-hand detail pane: the highlighted deck's name + picture,
  // or a "select a deck" hint when nothing is selected.
  void drawDeckDetail(sf::RenderTarget &target, const sf::Font &font,
                      const sf::Font &boldFont) const;

  std::optional<std::string> playerId_;
  std::optional<std::size_t> deleteIndex_;
  HomeAction pendingAction_ = HomeAction::None;
  float scale_ = 1.f;

  // Picker: one card per fixed profile (labels set once in the constructor).
  std::array<Button, 4> profileButtons_;
  sf::FloatRect gridPanel_{0.f, 0.f, 0.f, 0.f};

  // Vault.
  DeckListView deckList_;
  Button playButton_;
  Button newDeckButton_;
  Button editButton_;
  Button switchButton_;
  sf::FloatRect listRect_{0.f, 0.f, 0.f, 0.f};
  sf::FloatRect detailRect_{0.f, 0.f, 0.f, 0.f};
};

} // namespace mtgcpp::core
