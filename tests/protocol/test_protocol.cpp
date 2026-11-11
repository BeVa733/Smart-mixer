#include "SmartMixerProtocol.h"

#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>

using namespace smart_mixer::protocol;

static unsigned checks = 0;
#define CHECK(condition)                                                       \
  do {                                                                         \
    ++checks;                                                                  \
    if (!(condition)) {                                                        \
      std::cerr << __FILE__ << ':' << __LINE__ << ": " << #condition << '\n';  \
      std::exit(1);                                                            \
    }                                                                          \
  } while (false)

static Result receive(Parser &parser, const std::string &wire, Packet &packet,
                      uint32_t start = 0) {
  Result result = Result::None;
  for (size_t i = 0; i < wire.size(); ++i) {
    result = parser.feed(wire[i], start + static_cast<uint32_t>(i), packet);
  }
  return result;
}

// Deliberately permits invalid fields, so tests exercise validation after CRC.
static std::string frame(const std::string &body) {
  constexpr char hex[] = "0123456789ABCDEF";
  const uint16_t crc = crc16(body.data(), body.size());
  std::string result = "@" + body + '*';
  for (int shift = 12; shift >= 0; shift -= 4) {
    result += hex[(crc >> shift) & 15];
  }
  return result + '\n';
}

static void goldenVectors() {
  CHECK(crc16("123456789", 9) == 0x29B1);
  CHECK(crc16("", 0) == 0xFFFF);

  // Golden CRCs independently generated using Python binascii.crc_hqx.
  struct Vector {
    const char *wire;
    Type type;
    uint32_t session;
    uint32_t id;
  };
  const Vector vectors[] = {
      {"@1|0|1|CMD|HELLO|*9723\n", Type::Command, 0, 1},
      {"@1|42|17|CMD|DOSE|pump=2,volume_ul=50000*79A5\n", Type::Command, 42,
       17},
      {"@1|42|17|ACK|DOSE|*8982\n", Type::Ack, 42, 17},
      {"@1|42|17|DONE|DOSE|elapsed_ms=20000*1E99\n", Type::Done, 42, 17},
      {"@1|42|18|ERROR|MOVE|code=NOT_HOMED*5B05\n", Type::Error, 42, 18},
      {"@1|42|1|EVENT|BOOT|reason=RESET*85CF\n", Type::Event, 42, 1},
      {"@1|4294967295|4294967295|STATUS|GET_STATUS|*35D7\n", Type::Status,
       UINT32_MAX, UINT32_MAX}};
  for (const auto &v : vectors) {
    Parser parser;
    Packet packet;
    CHECK(receive(parser, v.wire, packet) == Result::Frame);
    CHECK(packet.type == v.type && packet.session == v.session &&
          packet.id == v.id);
    char encoded[kMaxFrameBytes];
    size_t written = 0;
    CHECK(encode(packet, encoded, sizeof(encoded), written));
    CHECK(std::string(encoded, written) == v.wire);
    CHECK(parser.counters().frames == 1);
  }
}

static void malformedAndRecovery() {
  struct Case {
    std::string body;
    Result error;
  };
  const Case cases[] = {
      {"2|0|1|CMD|HELLO|", Result::UnsupportedVersion},
      {"1|0|0|CMD|PING|", Result::InvalidNumber},
      {"1||1|CMD|PING|", Result::InvalidNumber},
      {"1|01|1|CMD|PING|", Result::InvalidNumber},
      {"1|-1|1|CMD|PING|", Result::InvalidNumber},
      {"1|4294967296|1|CMD|PING|", Result::InvalidNumber},
      {"1|0|4294967296|CMD|PING|", Result::InvalidNumber},
      {"1|0|1x|CMD|PING|", Result::InvalidNumber},
      {"1|0|1|BAD|PING|", Result::InvalidField},
      {"1|0|1|CMD||", Result::InvalidField},
      {"1|0|1|CMD|ping|", Result::InvalidField},
      {"1|0|1|CMD|1PING|", Result::InvalidField},
      {"1|0|1|CMD|PING|bad arg", Result::InvalidField},
      {"1|0|1|CMD|PING|bad*arg", Result::InvalidField},
      {"1|0|1|CMD|PING|bad|arg", Result::InvalidFormat},
      {"1|0|1|CMD|PING", Result::InvalidFormat},
      {"1|0|1|CMD|" + std::string(kMaxNameBytes + 1, 'A') + "|",
       Result::InvalidField},
      {"1|0|1|CMD|PING|" + std::string(kMaxArgumentBytes + 1, 'A'),
       Result::InvalidField}};
  for (const auto &c : cases) {
    Parser parser;
    Packet output;
    output.id = 123;
    CHECK(receive(parser, frame(c.body), output) == c.error);
    CHECK(output.id == 123); // Never publish a partially validated command.
    CHECK(receive(parser, "@1|0|1|CMD|HELLO|*9723\n", output, 1000) ==
          Result::Frame);
  }

  Parser parser;
  Packet packet;
  CHECK(receive(parser, "@1|0|1|CMD|HELLO|*9722\n", packet) ==
        Result::InvalidChecksum);
  CHECK(parser.counters().invalidChecksum == 1);
  CHECK(receive(parser, "@1|0|1|CMD|HELLO|*ZZZZ\n", packet, 1000) ==
        Result::InvalidFormat);
  CHECK(receive(parser, "junk\r\nboot message\n", packet, 2000) ==
        Result::None);
  CHECK(receive(parser, "@broken@1|0|1|CMD|HELLO|*9723\n", packet, 3000) ==
        Result::Frame);
  CHECK(parser.counters().resync == 1);

  CHECK(receive(parser, "@1|0|1|CMD|HELLO|*9723\r", packet, 4000) ==
        Result::InvalidFormat);
  CHECK(receive(parser, "@1|0|1|CMD|HELLO|*9723\n", packet, 5000) ==
        Result::Frame);
  CHECK(receive(parser, std::string("@bad\0data", 9), packet, 6000) ==
        Result::None);
  CHECK(receive(parser, "@1|0|1|CMD|HELLO|*9723\n", packet, 7000) ==
        Result::Frame);
}

static void boundsAndTimeouts() {
  Packet maximum;
  maximum.session = UINT32_MAX;
  maximum.id = UINT32_MAX;
  maximum.type = Type::Status;
  std::memset(maximum.name, 'N', kMaxNameBytes);
  std::memset(maximum.arguments, 'a', kMaxArgumentBytes);
  char encoded[kMaxFrameBytes];
  size_t written = 0;
  CHECK(encode(maximum, encoded, sizeof(encoded), written));
  CHECK(written <= kMaxFrameBytes);
  Parser parser;
  Packet packet;
  CHECK(receive(parser, std::string(encoded, written), packet) ==
        Result::Frame);
  CHECK(std::strcmp(packet.arguments, maximum.arguments) == 0);

  char small[8];
  std::memset(small, '?', sizeof(small));
  written = 999;
  CHECK(!encode(maximum, small, sizeof(small), written));
  CHECK(written == 0 && small[0] == '?' && small[7] == '?');
  CHECK(!encode(maximum, nullptr, kMaxFrameBytes, written));
  maximum.id = 0;
  CHECK(!encode(maximum, encoded, sizeof(encoded), written));
  maximum.id = 1;
  maximum.type = static_cast<Type>(255);
  CHECK(!encode(maximum, encoded, sizeof(encoded), written));
  maximum.type = Type::Command;
  std::memset(maximum.name, 'X', sizeof(maximum.name)); // Missing NUL.
  CHECK(!encode(maximum, encoded, sizeof(encoded), written));
  std::strcpy(maximum.name, "PING");
  std::memset(maximum.arguments, 'X', sizeof(maximum.arguments));
  CHECK(!encode(maximum, encoded, sizeof(encoded), written));

  parser.reset();
  CHECK(parser.feed('@', 1000, packet) == Result::None);
  for (size_t i = 0; i < kMaxFrameBytes - 2; ++i) {
    CHECK(parser.feed('a', 1001, packet) == Result::None);
  }
  CHECK(parser.feed('a', 1002, packet) == Result::Overflow);
  CHECK(parser.counters().overflow == 1);
  CHECK(receive(parser, "@1|0|1|CMD|HELLO|*9723\n", packet, 2000) ==
        Result::Frame);

  CHECK(parser.feed('@', 3000, packet) == Result::None);
  CHECK(parser.tick(3000 + kFrameTimeoutMs - 1) == Result::None);
  CHECK(parser.tick(3000 + kFrameTimeoutMs) == Result::Timeout);
  CHECK(parser.tick(4000) == Result::None);
  CHECK(parser.counters().timeout == 1);
  CHECK(parser.feed('@', UINT32_MAX - 100, packet) == Result::None);
  CHECK(parser.tick(149) == Result::Timeout); // millis() rollover, 250 ms.
  CHECK(parser.feed('@', 5000, packet) == Result::None);
  CHECK(parser.feed('@', 5250, packet) == Result::Timeout);
  CHECK(receive(parser, "1|0|1|CMD|HELLO|*9723\n", packet, 5251) ==
        Result::Frame);
}

static void fragmentationAndNoise() {
  const std::string wire = "@1|0|1|CMD|HELLO|*9723\n";
  // Flip every individual bit, including delimiters, CRC and terminator.
  // A corrupt frame must never be delivered and must not poison the next one.
  for (size_t offset = 0; offset < wire.size(); ++offset) {
    for (unsigned bit = 0; bit < 8; ++bit) {
      Parser corruptedParser;
      Packet unchanged;
      unchanged.id = 123;
      std::string damaged = wire;
      damaged[offset] = static_cast<char>(damaged[offset] ^ (1U << bit));
      receive(corruptedParser, damaged, unchanged);
      CHECK(corruptedParser.counters().frames == 0);
      CHECK(unchanged.id == 123);
      CHECK(receive(corruptedParser, wire, unchanged, 1000) == Result::Frame);
    }
  }
  for (size_t split = 0; split <= wire.size(); ++split) {
    Parser parser;
    Packet packet;
    receive(parser, wire.substr(0, split), packet);
    receive(parser, wire.substr(split), packet, 50);
    CHECK(parser.counters().frames == 1);
  }
  Parser parser;
  Packet packet;
  receive(parser, wire + wire + wire, packet);
  CHECK(parser.counters().frames == 3);

  // Deterministic arbitrary byte streams must not publish unchecked fields or
  // prevent recovery. ASan/UBSan check all buffer boundaries during this run.
  uint32_t random = 0x12345678;
  for (uint32_t i = 0; i < 100000; ++i) {
    random = random * 1664525U + 1013904223U;
    const Result result =
        parser.feed(static_cast<char>(random >> 24), i, packet);
    CHECK(result != Result::Frame);
    if (i % 1000 == 0) {
      for (char c : wire)
        parser.feed(c, i, packet);
    }
  }
  CHECK(receive(parser, wire, packet, 100001) == Result::Frame);
}

int main() {
  goldenVectors();
  malformedAndRecovery();
  boundsAndTimeouts();
  fragmentationAndNoise();
  std::cout << checks << " checks passed\n";
}
