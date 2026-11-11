#include "SmartMixerProtocol.h"

#include <string.h>

namespace smart_mixer {
namespace protocol {
namespace {

const char *typeName(Type type) {
  switch (type) {
  case Type::Command:
    return "CMD";
  case Type::Ack:
    return "ACK";
  case Type::Done:
    return "DONE";
  case Type::Error:
    return "ERROR";
  case Type::Status:
    return "STATUS";
  case Type::Event:
    return "EVENT";
  }
  return nullptr;
}

bool parseType(const char *text, Type &type) {
  const Type values[] = {Type::Command, Type::Ack,    Type::Done,
                         Type::Error,   Type::Status, Type::Event};
  for (Type candidate : values) {
    if (strcmp(text, typeName(candidate)) == 0) {
      type = candidate;
      return true;
    }
  }
  return false;
}

bool upper(char c) { return c >= 'A' && c <= 'Z'; }
bool digit(char c) { return c >= '0' && c <= '9'; }

bool validName(const char *text, size_t &length) {
  length = 0;
  if (!upper(text[0]))
    return false;
  while (length <= kMaxNameBytes) {
    const char c = text[length];
    if (c == '\0')
      return true;
    if (!upper(c) && !digit(c) && c != '_')
      return false;
    ++length;
  }
  return false;
}

bool validArguments(const char *text, size_t &length) {
  length = 0;
  while (length <= kMaxArgumentBytes) {
    const char c = text[length];
    if (c == '\0')
      return true;
    if (!upper(c) && !(c >= 'a' && c <= 'z') && !digit(c) && c != '_' &&
        c != ',' && c != '=' && c != '.' && c != '+' && c != '-') {
      return false;
    }
    ++length;
  }
  return false;
}

bool parseNumber(const char *text, uint32_t &value) {
  if (!digit(text[0]) || (text[0] == '0' && text[1] != '\0'))
    return false;
  uint32_t parsed = 0;
  for (size_t i = 0; text[i] != '\0'; ++i) {
    if (!digit(text[i]))
      return false;
    const uint32_t d = static_cast<uint32_t>(text[i] - '0');
    if (parsed > (UINT32_MAX - d) / 10U)
      return false;
    parsed = parsed * 10U + d;
  }
  value = parsed;
  return true;
}

int hexValue(char c) {
  if (digit(c))
    return c - '0';
  if (c >= 'A' && c <= 'F')
    return c - 'A' + 10;
  return -1;
}

// Private scratch storage means invalid/capacity-limited encoding never leaks
// a partial frame into the caller's output buffer.
struct Writer {
  char bytes[kMaxFrameBytes];
  size_t used = 0;
  bool append(char c) {
    if (used == sizeof(bytes))
      return false;
    bytes[used++] = c;
    return true;
  }
  bool append(const char *s) {
    for (; *s; ++s)
      if (!append(*s))
        return false;
    return true;
  }
  bool number(uint32_t n) {
    char reverse[10];
    size_t count = 0;
    do {
      reverse[count++] = static_cast<char>('0' + n % 10U);
      n /= 10U;
    } while (n != 0);
    while (count != 0)
      if (!append(reverse[--count]))
        return false;
    return true;
  }
};

} // namespace

uint16_t crc16(const char *bytes, size_t length) {
  uint16_t crc = 0xFFFF;
  for (size_t i = 0; i < length; ++i) {
    crc ^= static_cast<uint16_t>(static_cast<uint8_t>(bytes[i])) << 8;
    for (uint8_t bit = 0; bit < 8; ++bit) {
      crc = static_cast<uint16_t>((crc & 0x8000U) != 0 ? (crc << 1) ^ 0x1021U
                                                       : crc << 1);
    }
  }
  return crc;
}

bool encode(const Packet &packet, char *output, size_t capacity,
            size_t &written) {
  written = 0;
  size_t nameLength = 0, argumentLength = 0;
  const char *type = typeName(packet.type);
  if (output == nullptr || packet.id == 0 || type == nullptr ||
      !validName(packet.name, nameLength) ||
      !validArguments(packet.arguments, argumentLength))
    return false;

  Writer w;
  if (!w.append("@1|") || !w.number(packet.session) || !w.append('|') ||
      !w.number(packet.id) || !w.append('|') || !w.append(type) ||
      !w.append('|') || !w.append(packet.name) || !w.append('|') ||
      !w.append(packet.arguments))
    return false;
  const uint16_t crc = crc16(w.bytes + 1, w.used - 1);
  if (!w.append('*'))
    return false;
  constexpr char hex[] = "0123456789ABCDEF";
  for (int shift = 12; shift >= 0; shift -= 4) {
    if (!w.append(hex[(crc >> shift) & 0x0F]))
      return false;
  }
  if (!w.append('\n') || capacity < w.used)
    return false;
  memcpy(output, w.bytes, w.used);
  written = w.used;
  return true;
}

void Parser::reset() {
  receiving_ = false;
  length_ = 0;
}

Result Parser::record(Result result) {
  switch (result) {
  case Result::Frame:
    ++counters_.frames;
    break;
  case Result::InvalidFormat:
    ++counters_.invalidFormat;
    break;
  case Result::InvalidChecksum:
    ++counters_.invalidChecksum;
    break;
  case Result::UnsupportedVersion:
    ++counters_.unsupportedVersion;
    break;
  case Result::InvalidNumber:
    ++counters_.invalidNumber;
    break;
  case Result::InvalidField:
    ++counters_.invalidField;
    break;
  case Result::Overflow:
    ++counters_.overflow;
    break;
  case Result::Timeout:
    ++counters_.timeout;
    break;
  case Result::None:
    break;
  }
  return result;
}

Result Parser::tick(uint32_t nowMs) {
  if (receiving_ &&
      static_cast<uint32_t>(nowMs - startedAt_) >= kFrameTimeoutMs) {
    reset();
    return record(Result::Timeout);
  }
  return Result::None;
}

Result Parser::feed(char byte, uint32_t nowMs, Packet &output) {
  const Result expired = tick(nowMs);
  if (byte == '@') {
    if (receiving_)
      ++counters_.resync;
    receiving_ = true;
    startedAt_ = nowMs;
    length_ = 0;
    return expired;
  }
  if (!receiving_)
    return expired;

  if (byte == '\n') {
    // @ and LF count towards the maximum wire length. NUL replaces LF
    // inside the private parser buffer, never inside the wire protocol.
    buffer_[length_] = '\0';
    receiving_ = false;
    const Result result = decode(output);
    length_ = 0;
    return record(result);
  }
  if (length_ >= kMaxFrameBytes - 2) {
    reset();
    return record(Result::Overflow);
  }
  if (byte < 0x20 || byte > 0x7E) {
    reset();
    return record(Result::InvalidFormat);
  }
  buffer_[length_++] = byte;
  return Result::None;
}

Result Parser::decode(Packet &output) {
  if (length_ < 5 || buffer_[length_ - 5] != '*')
    return Result::InvalidFormat;
  const size_t bodyLength = length_ - 5;
  uint16_t expected = 0;
  for (size_t i = bodyLength + 1; i < length_; ++i) {
    const int h = hexValue(buffer_[i]);
    if (h < 0)
      return Result::InvalidFormat;
    expected = static_cast<uint16_t>((expected << 4) | h);
  }
  if (expected != crc16(buffer_, bodyLength))
    return Result::InvalidChecksum;
  buffer_[bodyLength] = '\0';

  char *fields[6] = {buffer_, nullptr, nullptr, nullptr, nullptr, nullptr};
  size_t fieldCount = 1;
  for (size_t i = 0; i < bodyLength; ++i) {
    if (buffer_[i] == '|') {
      if (fieldCount == 6)
        return Result::InvalidFormat;
      buffer_[i] = '\0';
      fields[fieldCount++] = buffer_ + i + 1;
    }
  }
  if (fieldCount != 6)
    return Result::InvalidFormat;
  if (strcmp(fields[0], "1") != 0)
    return Result::UnsupportedVersion;

  Packet packet;
  if (!parseNumber(fields[1], packet.session) ||
      !parseNumber(fields[2], packet.id) || packet.id == 0) {
    return Result::InvalidNumber;
  }
  size_t nameLength = 0, argumentLength = 0;
  if (!parseType(fields[3], packet.type) || !validName(fields[4], nameLength) ||
      !validArguments(fields[5], argumentLength))
    return Result::InvalidField;
  memcpy(packet.name, fields[4], nameLength + 1);
  memcpy(packet.arguments, fields[5], argumentLength + 1);
  output = packet;
  return Result::Frame;
}

} // namespace protocol
} // namespace smart_mixer
