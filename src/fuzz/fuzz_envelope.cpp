// Fuzz target: the network envelope + length-prefixed framing (M11.1).
//
// Exercises the full untrusted-wire path: envelope schema validation
// (`parseEnvelope`), the incremental `FrameDecoder` (whole-buffer AND
// byte-by-byte feeding, so buffering/oversize handling is covered), and the
// frame encoder. Nothing here may throw or leak; `parseEnvelope` is
// documented to return nullopt on malformed input rather than throw.

#include "fuzz/fuzz_target.h"
#include "net/envelope.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, std::size_t size) {
  std::string bytes(size, '\0');
  if (size != 0u) {
    std::memcpy(bytes.data(), data, size);
  }
  const std::string_view input(bytes.data(), bytes.size());

  // Envelope schema validation.
  (void)mtgcpp::net::parseEnvelope(input);

  // Frame decoding, whole-buffer: an oversize header must be rejected and the
  // decoder must stop producing frames (never trust the wire).
  mtgcpp::net::FrameDecoder whole;
  whole.push(input);
  for (;;) {
    if (!whole.next().has_value()) {
      break;
    }
  }

  // Frame decoding, byte-by-byte: exercises incremental buffering, where a
  // length header can be split across reads.
  mtgcpp::net::FrameDecoder incremental;
  for (const char byte : bytes) {
    incremental.push(std::string_view(&byte, 1));
    (void)incremental.next();
  }

  // Encoder round-trip: encode what fits, decode it back.
  const std::string encoded = mtgcpp::net::Framing::encodeFrame(input);
  if (!encoded.empty()) {
    mtgcpp::net::FrameDecoder roundTrip;
    roundTrip.push(encoded);
    (void)roundTrip.next();
  }
  return 0;
}

#ifdef MTG_CPP_FUZZ_DRIVER
int main(int argc, char **argv) {
  return mtgcpp::fuzz::runDriverMain(&LLVMFuzzerTestOneInput, argc, argv,
                                     "src/fuzz/seed_corpus/envelope");
}
#endif
