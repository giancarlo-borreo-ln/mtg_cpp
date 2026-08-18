// Local network-address helpers (Sprint 8).
//
// The host shares its `IP:PORT` so a remote peer can connect to the embedded
// relay. `localIpAddress()` resolves the machine's hostname and returns the
// first non-loopback IPv4 address — the address a peer on the LAN would use —
// falling back to 127.0.0.1 when no external address is found (e.g. no network
// or a hostname that resolves only to loopback).
#pragma once

#include <string>

namespace mtgcpp::net {

// Best-effort local IPv4 address for the host to share. Never empty.
std::string localIpAddress();

} // namespace mtgcpp::net
