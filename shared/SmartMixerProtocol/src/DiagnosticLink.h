#pragma once

#include "ProtocolTransport.h"

namespace smart_mixer {
namespace protocol {

// Read-only snapshot: this layer has no actuator callbacks by design.
struct MotionSnapshot {
  bool homed = false;
  bool upperLimit = false;
  bool pump1 = false;
  bool pump2 = false;
  int32_t position = 0;
};

class DiagnosticServer {
public:
  void tick(uint32_t now);
  void handle(const Packet &request, uint32_t now,
              const MotionSnapshot &snapshot, Transport &transport);
  uint32_t session() const { return session_; }
  uint32_t rejected() const { return rejected_; }

private:
  uint32_t session_ = 0;
  uint32_t nonce_ = 0;
  uint32_t lastRequestAt_ = 0;
  bool challenged_ = false;
  uint32_t rejected_ = 0;
};

enum class LinkState : uint8_t {
  Offline,
  Hello,
  Opening,
  Synchronizing,
  Ready
};

class DiagnosticClient {
public:
  // Caller supplies a fresh random, nonzero token for every reconnect attempt.
  // No EEPROM writes or persistent session counter are needed for diagnostics.
  void tick(uint32_t now, uint32_t freshToken, Transport &transport);
  void handle(const Packet &response, uint32_t now, Transport &transport);
  bool requestPing() {
    if (state_ != LinkState::Ready)
      return false;
    pingRequested_ = true;
    return true;
  }
  LinkState state() const { return state_; }
  uint32_t session() const { return session_; }
  uint32_t rttMs() const { return rttMs_; }
  uint32_t timeouts() const { return timeouts_; }
  uint32_t unexpected() const { return unexpected_; }
  uint32_t pongs() const { return pongs_; }
  const MotionSnapshot &motion() const { return motion_; }
  const char *lastError() const { return lastError_; }

private:
  void disconnect(uint32_t now, Transport &transport, const char *error);
  bool send(const char *name, uint32_t now, Transport &transport);

  LinkState state_ = LinkState::Offline;
  Packet pending_;
  bool waiting_ = false;
  bool pingRequested_ = false;
  bool reconnectDelay_ = false;
  uint8_t retries_ = 0;
  uint32_t session_ = 0;
  uint32_t nextId_ = 1;
  uint32_t sentAt_ = 0;
  uint32_t requestStartedAt_ = 0;
  uint32_t lastResponseAt_ = 0;
  uint32_t retryAfter_ = 0;
  uint32_t lastPingAt_ = 0;
  uint32_t lastStatusAt_ = 0;
  uint32_t rttMs_ = 0;
  uint32_t timeouts_ = 0;
  uint32_t unexpected_ = 0;
  uint32_t pongs_ = 0;
  MotionSnapshot motion_;
  char lastError_[32] = "NONE";
};

} // namespace protocol
} // namespace smart_mixer
