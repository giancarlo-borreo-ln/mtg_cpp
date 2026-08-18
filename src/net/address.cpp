// Local network-address helpers (Sprint 8).
#include "net/address.h"

#include <asio.hpp>

#include <string>

namespace mtgcpp::net {

std::string localIpAddress() {
  asio::error_code ec;
  // Resolve the local hostname (asio's resolver is safe on any platform).
  const std::string hostname = asio::ip::host_name(ec);
  if (ec) {
    return "127.0.0.1";
  }
  asio::io_context ioContext;
  asio::ip::tcp::resolver resolver(ioContext);
  const asio::ip::tcp::resolver::results_type results = resolver.resolve(hostname, "", ec);
  if (ec) {
    return "127.0.0.1";
  }
  for (const auto &entry : results) {
    const asio::ip::address &address = entry.endpoint().address();
    // Prefer the first routable IPv4; loopback is the fallback.
    if (address.is_v4() && !address.is_loopback()) {
      return address.to_string();
    }
  }
  return "127.0.0.1";
}

} // namespace mtgcpp::net
