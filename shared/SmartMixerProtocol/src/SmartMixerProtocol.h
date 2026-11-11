#pragma once

#include <stddef.h>
#include <stdint.h>

namespace smart_mixer {
namespace protocol {

constexpr uint8_t kVersion = 1;
constexpr size_t kMaxFrameBytes = 192; // Includes @ and LF; no terminating NUL.
constexpr size_t kMaxNameBytes = 24;
constexpr size_t kMaxArgumentBytes = 128;
constexpr uint32_t kFrameTimeoutMs = 250;

enum class Type : uint8_t { Command, Ack, Done, Error, Status, Event };
enum class Result : uint8_t {
  None,
  Frame,
  InvalidFormat,
  InvalidChecksum,
  UnsupportedVersion,
  InvalidNumber,
  InvalidField,
  Overflow,
  Timeout
};

struct Packet {
  uint32_t session = 0;
  uint32_t id = 0;
  Type type = Type::Command;
  char name[kMaxNameBytes + 1] = {};
  char arguments[kMaxArgumentBytes + 1] = {};
};

struct Counters {
  uint32_t frames = 0;
  uint32_t invalidFormat = 0;
  uint32_t invalidChecksum = 0;
  uint32_t unsupportedVersion = 0;
  uint32_t invalidNumber = 0;
  uint32_t invalidField = 0;
  uint32_t overflow = 0;
  uint32_t timeout = 0;
  uint32_t resync = 0;
};

// CRC-16/CCITT-FALSE: poly 0x1021, init 0xFFFF, no reflection, xorout 0.
uint16_t crc16(const char *bytes, size_t length);

// output receives a complete wire frame, without a terminating NUL. On failure
// written is zero and output must not be sent. Arguments never allocate memory.
bool encode(const Packet &packet, char *output, size_t capacity,
            size_t &written);

class Parser {
public:
  // Feed every received byte in order. Packet output changes only on Frame.
  // A new @ always starts a fresh candidate, even after malformed input.
  Result feed(char byte, uint32_t nowMs, Packet &output);
  // Call even when UART is silent, to expire incomplete frames.
  Result tick(uint32_t nowMs);
  void reset(); // Discards partial input; preserves diagnostic counters.
  const Counters &counters() const { return counters_; }

private:
  Result decode(Packet &output);
  Result record(Result result);

  char buffer_[kMaxFrameBytes] = {};
  size_t length_ = 0;
  uint32_t startedAt_ = 0;
  bool receiving_ = false;
  Counters counters_;
};

} // namespace protocol
} // namespace smart_mixer
