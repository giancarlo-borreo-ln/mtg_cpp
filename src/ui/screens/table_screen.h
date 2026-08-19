// The Table screen (M9.4 + M9.5): the Shandalar battlefield.
//
// Per the locked §1b decision this is NOT the webapp's drag-and-drop CSS grid:
// it is a central velvet mat with arched hand rows (opponent top, you bottom),
// two 3-column zone grids, the Stack in the middle as the only overlap, and
// click + command interaction — click a card to select it, then act via the
// context menu or keyboard verbs (T tap, C counter, K token, F flip, S stack,
// M move-to-zone). Instant snap, zero animation.
//
// The screen owns no I/O: it renders the board the App pushes in (setBoard +
// reveal state) and reports TableActions. For card commands it builds the
// state::BoardAction itself (it has the selected card's seat/zone/id), so the
// App just applies it through the session. Everything except draw() is pure and
// headless-testable.
#pragma once

#include "core/board.h"
#include "state/board_state.h"
#include "ui/cursor.h"
#include "ui/widgets/button.h"
#include "ui/widgets/card_view.h"
#include "ui/widgets/context_menu.h"
#include "ui/widgets/zone_view.h"

#include <SFML/Graphics/Font.hpp>
#include <SFML/Graphics/Rect.hpp>
#include <SFML/Graphics/RenderTarget.hpp>
#include <SFML/Graphics/Texture.hpp>
#include <SFML/System/Vector2.hpp>
#include <SFML/Window/Event.hpp>

#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace mtgcpp::core {

class ArtCache; // fwd: attached via setArtCache (owned by the App)

// What the user just did on the Table. CardCommand carries a fully-built
// state::BoardAction (tap/counter/token/flip/move/life) that the App applies
// through the session; the rest are session-level flows.
enum class TableAction {
  None,
  CardCommand,   // apply action()
  RequestReveal, // request the opponent's hand
  AcceptReveal,  // App reads revealCards() for the payload
  DenyReveal,
  DismissReveal,
  ClearRevealed,
  Leave,
};

// The card the user selected (or right-clicked). `zone` is the zone the card
// lives in (nullopt for hand/stack cards); the card snapshot lets the screen
// build commands without re-searching the board.
struct CardSelection {
  PlayerSeat seat = PlayerSeat::Host;
  bool in_hand = false;
  bool in_stack = false;
  std::optional<PlayerZone> zone;
  std::size_t index = 0;
  BoardCard card;

  bool operator==(const CardSelection &) const = default;
};

class TableScreen {
public:
  TableScreen();

  // --- Board state (pushed by the App every frame) --------------------------
  void setBoard(const state::BoardState &board);
  const state::BoardState &board() const { return board_; }
  void setRole(std::optional<PlayerSeat> role);
  void setPlayerId(std::optional<std::string> playerId);
  // Hand-reveal consent flow: who is asking, and the last result.
  void setRevealRequest(std::optional<std::string> from);
  void setRevealResult(std::optional<bool> accepted, std::vector<RevealCard> hand);
  void setError(std::optional<std::string> error);

  // --- Layout ---------------------------------------------------------------
  void relayout(const sf::FloatRect &content, float scale);

  // --- Cursor (M10.3 polish) ------------------------------------------------
  // The pointer over `point`: Hand over anything clickable (your cards, the
  // life rings, the toolbar/menu/reveal buttons), Arrow elsewhere. Pure.
  CursorKind cursorAt(sf::Vector2f point) const;

  // --- Art cache (M10.1) ----------------------------------------------------
  // Attach the runtime art cache (nullptr = always procedural). The screen
  // requests art for every face-up card on relayout, drains completed downloads
  // and builds per-card textures in pumpArt(), and falls back to the procedural
  // front texture whenever art is missing / still downloading / offline.
  void setArtCache(ArtCache *cache);
  // Drain the cache and build textures for cards whose art just arrived. Called
  // once per frame by the App (main thread, needs a GL context).
  void pumpArt();
  // The cached art texture for a face-up card, or nullptr (procedural front).
  // Read-only; exposed so tests can assert the fallback path.
  const sf::Texture *artTextureFor(const BoardCard &card) const;

  // --- Interaction ----------------------------------------------------------
  bool routeEvent(const sf::Event &event);
  TableAction pollAction();

  void draw(sf::RenderTarget &target, const sf::Font &font, const sf::Font &boldFont) const;

  // --- Exposed for tests ----------------------------------------------------
  // The action queued by CardCommand (meaningful only when pollAction returned
  // CardCommand).
  const state::BoardAction &action() const { return action_; }
  // The hand-reveal accept payload (the local hand, projected).
  std::vector<RevealCard> revealCards() const;
  // Pure mouse/keyboard entry points (routeEvent translates sf::Event into
  // these), mirroring the menu widgets' convention.
  bool mousePressed(sf::Vector2f point);
  bool mouseReleased(sf::Vector2f point);
  bool keyPressed(sf::Event::KeyEvent key);
  // The currently selected card (nullopt when none / a read-only card).
  const std::optional<CardSelection> &selection() const { return selection_; }
  // The command menu state.
  bool menuOpen() const { return menuOpen_; }
  const ContextMenu &menu() const { return menu_; }
  const std::vector<std::string> &menuItems() const { return menu_.items(); }
  // The zone being edited for life (nullopt when not editing).
  std::optional<PlayerSeat> lifeEditingSeat() const { return lifeEditingSeat_; }
  std::string lifeEditText() const { return lifeEditText_; }
  // The life-ring rects (seat -> ring), so tests can click them.
  const std::array<sf::FloatRect, 2> &lifeRings() const { return life_rings_; }
  // Exposed widget handles for the reveal/leave flows.
  const Button &leaveButton() const { return leaveButton_; }
  const Button &revealButton() const { return revealButton_; }
  const Button &acceptButton() const { return acceptButton_; }
  const Button &denyButton() const { return denyButton_; }
  const Button &dismissButton() const { return dismissButton_; }
  const Button &closeRevealedButton() const { return closeRevealedButton_; }

private:
  // Per-zone geometry for one board half (tile rect + card tiles).
  struct ZoneLayout {
    sf::FloatRect zone_rect;
    std::vector<sf::FloatRect> card_rects;
  };

  PlayerSeat mySeat() const { return role_.value_or(PlayerSeat::Host); }
  PlayerSeat theirSeat() const { return mtgcpp::core::oppositeSeat(mySeat()); }
  // The pile a card lives in, resolved for hit-testing + commands.
  const std::vector<BoardCard> &pileFor(PlayerSeat seat, bool in_hand,
                                        std::optional<PlayerZone> zone, bool in_stack) const;

  // Hit-test: the card at `point` (top-most on overlap), or nullopt.
  std::optional<CardSelection> cardAt(sf::Vector2f point) const;
  // The slot rect of the current selection (for the highlight + menu anchor).
  std::optional<sf::FloatRect> selectedCardRect() const;
  // Close the command menu (returns to card-selection state).
  void closeMenu();
  // Rebuild the context menu items from the current selection state.
  void rebuildMenu();
  // Run a queued menu selection / keyboard verb against the selection.
  void runCommand(std::size_t menuIndex);
  // Move the selected card to a target (zone or the Stack).
  void moveSelection(PlayerZone zone, bool toStack);
  // Begin / commit / cancel life editing.
  void beginLifeEdit(PlayerSeat seat);
  void commitLifeEdit();
  void cancelLifeEdit();
  // Right-click handling (opens the command menu on a card).
  bool mousePressedRight(sf::Vector2f point);
  // Face-up cards whose art should be requested/rendered (my hand + both
  // halves' zones; tokens, face-down and id-less cards excluded).
  std::vector<const BoardCard *> artCards() const;
  static bool cardUsesArt(const BoardCard &card);
  // --- Rendering helpers (windowed) ------------------------------------------
  void drawHand(sf::RenderTarget &target, const sf::Font &font, PlayerSeat seat) const;
  void drawZones(sf::RenderTarget &target, const sf::Font &font, PlayerSeat seat) const;
  void drawStack(sf::RenderTarget &target, const sf::Font &font) const;
  static void drawFace(sf::RenderTarget &target, const sf::FloatRect &rect,
                       const sf::Texture &texture);
  void drawLifeRing(sf::RenderTarget &target, const sf::Font &font, PlayerSeat seat) const;

  // Layout caches (recomputed in relayout; indexed by PlayerSeat).
  std::array<sf::FloatRect, 2> hand_rects_;
  std::array<std::array<ZoneLayout, kPlayerZoneCount>, 2> zones_;
  std::vector<sf::FloatRect> stack_rects_;
  sf::Vector2f stack_card_size_{0.f, 0.f};
  sf::FloatRect stack_base_{0.f, 0.f, 0.f, 0.f};
  std::array<sf::FloatRect, 2> life_rings_; // seat -> ring rect
  sf::FloatRect toolbar_{0.f, 0.f, 0.f, 0.f};
  sf::FloatRect battlefield_{0.f, 0.f, 0.f, 0.f};
  sf::FloatRect revealPromptRect_{0.f, 0.f, 0.f, 0.f};
  sf::FloatRect revealedPanelRect_{0.f, 0.f, 0.f, 0.f};

  // View state (pushed by the App).
  state::BoardState board_;
  std::optional<PlayerSeat> role_;
  std::optional<std::string> playerId_;
  std::optional<std::string> revealRequestFrom_;
  std::optional<bool> revealAccepted_;
  std::vector<RevealCard> revealedHand_;
  std::optional<std::string> error_;

  // Interaction state.
  std::optional<CardSelection> selection_;
  bool menuOpen_ = false;
  bool moveMenu_ = false; // true while the menu lists move targets
  sf::Vector2f menuPosition_{0.f, 0.f};
  std::optional<PlayerSeat> lifeEditingSeat_;
  std::string lifeEditText_;
  std::string lifeEditOriginal_; // value when editing started (first digit replaces)
  std::optional<TableAction> queuedAction_;
  state::BoardAction action_;

  // Procedural textures (rebuilt on relayout when the size changes).
  sf::Texture frontTex_;
  sf::Texture backTex_;
  sf::Texture tokenTex_;
  sf::Texture tileTex_;
  sf::Texture ringTex_;
  sf::Vector2u textureSize_{0u, 0u};

  // Art cache (M10.1): not owned. Per-card art textures keyed by scryfall id,
  // built on the main thread from cache images (procedural fallback otherwise).
  ArtCache *artCache_ = nullptr;
  std::unordered_map<std::string, sf::Texture> artTextures_;

  // Widgets.
  Button leaveButton_;
  Button revealButton_;
  Button acceptButton_;
  Button denyButton_;
  Button dismissButton_;
  Button closeRevealedButton_;
  ContextMenu menu_;

  float scale_ = 1.f;
  sf::FloatRect content_{0.f, 0.f, 0.f, 0.f};
};

} // namespace mtgcpp::core
