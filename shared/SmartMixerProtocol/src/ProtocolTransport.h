#pragma once

#include "SmartMixerProtocol.h"
#include <string.h>

namespace smart_mixer {
namespace protocol {

constexpr uint32_t kLinkBaud = 19200;
constexpr uint32_t kRequestTimeoutMs = 500;
constexpr uint32_t kLinkTimeoutMs = 2000;
constexpr uint32_t kPingPeriodMs = 500;
constexpr uint32_t kStatusPeriodMs = 1000;

// A single owner calls receive()/transmit(); no ISR access and no allocations.
// Reader: available(), read(). Writer: tryWrite(bytes, length) returning the
// count actually accepted without waiting. A UART-specific adapter owns pacing.
class Transport {
public:
  bool enqueue(const Packet &packet) {
    if (count_ == kQueueDepth) {
      ++dropped_;
      return false;
    }
    Slot &slot = queue_[(head_ + count_) % kQueueDepth];
    if (!encode(packet, slot.bytes, sizeof(slot.bytes), slot.length)) {
      ++dropped_;
      return false;
    }
    slot.offset = 0;
    ++count_;
    return true;
  }

  template <class Reader, class Handler>
  void receive(Reader &reader, uint32_t now, Handler handler) {
    parser_.tick(now);
    Packet packet;
    for (size_t i = 0; i < 64 && reader.available() > 0; ++i) {
      const int byte = reader.read();
      if (byte < 0)
        break;
      if (parser_.feed(static_cast<char>(byte), now, packet) == Result::Frame) {
        handler(packet, now);
      }
    }
  }

  template <class Writer> void transmit(Writer &writer) {
    if (count_ == 0)
      return;
    Slot &slot = queue_[head_];
    const size_t remaining = slot.length - slot.offset;
    const size_t budget = remaining < 32 ? remaining : 32;
    const size_t accepted = writer.tryWrite(slot.bytes + slot.offset, budget);
    if (accepted > budget) {
      ++dropped_;
      clearPending();
      return;
    }
    slot.offset += accepted;
    if (slot.offset == slot.length) {
      head_ = (head_ + 1) % kQueueDepth;
      --count_;
      ++sent_;
    }
  }

  // Drop queued old requests on reconnect. The peer parser resynchronizes at @
  // even when the last transmitted frame was incomplete. Keep error counters.
  void clearPending() {
    head_ = 0;
    count_ = 0;
    parser_.reset();
  }
  bool idle() const { return count_ == 0; }
  uint32_t sent() const { return sent_; }
  uint32_t dropped() const { return dropped_; }
  const Counters &received() const { return parser_.counters(); }

private:
  static constexpr size_t kQueueDepth = 4;
  struct Slot {
    char bytes[kMaxFrameBytes];
    size_t length = 0;
    size_t offset = 0;
  };
  Slot queue_[kQueueDepth];
  size_t head_ = 0;
  size_t count_ = 0;
  Parser parser_;
  uint32_t sent_ = 0;
  uint32_t dropped_ = 0;
};

} // namespace protocol
} // namespace smart_mixer
