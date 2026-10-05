// Shared widget infrastructure (M4.2): the process-wide clipboard reader the
// text widgets' paste uses. The default reads the OS clipboard; tests assign a
// fake so paste is headless-verifiable. Defined in a single .cpp so both
// TextInput and TextArea share one registry.
#include "ui/widgets/widget.h"

#include <SFML/Window/Clipboard.hpp>

namespace mtgcpp::core {

ClipboardReader &clipboardReader() {
  static ClipboardReader reader = [] { return sf::Clipboard::getString(); };
  return reader;
}

} // namespace mtgcpp::core
