#include "DiagnosticLink.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

namespace smart_mixer {
namespace protocol {
namespace {

bool number(const char *text, size_t length, uint32_t &out) {
  if (length == 0 || length > 10 || (length > 1 && text[0] == '0'))
    return false;
  uint32_t n = 0;
  for (size_t i = 0; i < length; ++i) {
    if (text[i] < '0' || text[i] > '9')
      return false;
    const uint32_t d = static_cast<uint32_t>(text[i] - '0');
    if (n > (UINT32_MAX - d) / 10U)
      return false;
    n = n * 10U + d;
  }
  out = n;
  return true;
}

bool nonceArgument(const char *text, uint32_t &out) {
  return strncmp(text, "nonce=", 6) == 0 &&
         number(text + 6, strlen(text + 6), out) && out != 0;
}

bool parseSnapshot(const char *text, MotionSnapshot &out) {
  // Exact keys/order; no unknown fields, duplicates, whitespace or loose
  // sscanf.
  const char *keys[] = {"homed=", ",limit=", ",p1=", ",p2=", ",pos="};
  uint32_t values[5] = {};
  bool negative = false;
  for (size_t i = 0; i < 5; ++i) {
    const size_t prefix = strlen(keys[i]);
    if (strncmp(text, keys[i], prefix) != 0)
      return false;
    text += prefix;
    if (i == 4 && *text == '-') {
      negative = true;
      ++text;
    }
    const char *end = strchr(text, ',');
    if (i == 4) {
      if (end != nullptr)
        return false;
      end = text + strlen(text);
    } else if (end == nullptr)
      return false;
    if (!number(text, static_cast<size_t>(end - text), values[i]))
      return false;
    if (i < 4 && values[i] > 1)
      return false;
    text = end;
  }
  if ((!negative && values[4] > INT32_MAX) ||
      (negative && values[4] > static_cast<uint32_t>(INT32_MAX) + 1U))
    return false;
  MotionSnapshot snapshot;
  snapshot.homed = values[0] != 0;
  snapshot.upperLimit = values[1] != 0;
  snapshot.pump1 = values[2] != 0;
  snapshot.pump2 = values[3] != 0;
  snapshot.position =
      negative ? static_cast<int32_t>(-static_cast<int64_t>(values[4]))
               : static_cast<int32_t>(values[4]);
  out = snapshot;
  return true;
}

void errorReply(Packet &reply, const char *code) {
  reply.type = Type::Error;
  snprintf(reply.arguments, sizeof(reply.arguments), "code=%s", code);
}

} // namespace

void DiagnosticServer::tick(uint32_t now) {
  if ((session_ != 0 || challenged_) &&
      now - lastRequestAt_ >= kLinkTimeoutMs) {
    session_ = 0;
    challenged_ = false;
    nonce_ = 0;
  }
}

void DiagnosticServer::handle(const Packet &request, uint32_t now,
                              const MotionSnapshot &snapshot,
                              Transport &transport) {
  tick(now);
  if (request.type != Type::Command)
    return;
  Packet reply = request;
  reply.type = Type::Done;
  reply.arguments[0] = '\0';
  uint32_t nonce = 0;
  if (strcmp(request.name, "HELLO") == 0) {
    if (request.session != 0 || !nonceArgument(request.arguments, nonce)) {
      errorReply(reply, "BAD_ARGUMENTS");
    } else if (session_ != 0 && nonce != nonce_) {
      // A rebooted peer waits for the old read-only lease to expire.
      errorReply(reply, "BUSY");
    } else {
      challenged_ = true;
      nonce_ = nonce;
      lastRequestAt_ = now;
      snprintf(reply.arguments, sizeof(reply.arguments),
               "version=1,nonce=%" PRIu32 ",role=MOTION,features=DIAG", nonce_);
    }
  } else if (strcmp(request.name, "OPEN") == 0) {
    if (request.session == 0 || !nonceArgument(request.arguments, nonce)) {
      errorReply(reply, "BAD_ARGUMENTS");
    } else if (!challenged_ || nonce != nonce_ || request.session != nonce_ ||
               (session_ != 0 && request.session != session_)) {
      errorReply(reply, "WRONG_SESSION");
    } else {
      session_ = request.session;
      lastRequestAt_ = now;
      strcpy(reply.arguments, "mode=DIAG");
    }
  } else if (session_ == 0 || request.session != session_) {
    errorReply(reply, "WRONG_SESSION");
  } else if (request.arguments[0] != '\0') {
    errorReply(reply, "BAD_ARGUMENTS");
  } else if (strcmp(request.name, "PING") == 0) {
    lastRequestAt_ = now;
    strcpy(reply.arguments, "reply=PONG");
  } else if (strcmp(request.name, "GET_STATUS") == 0) {
    lastRequestAt_ = now;
    reply.type = Type::Status;
    snprintf(reply.arguments, sizeof(reply.arguments),
             "homed=%u,limit=%u,p1=%u,p2=%u,pos=%" PRId32,
             static_cast<unsigned>(snapshot.homed),
             static_cast<unsigned>(snapshot.upperLimit),
             static_cast<unsigned>(snapshot.pump1),
             static_cast<unsigned>(snapshot.pump2), snapshot.position);
  } else {
    // Includes STOP: no actuator access until the asynchronous executor exists.
    errorReply(reply, "UNSUPPORTED_COMMAND");
  }
  if (reply.type == Type::Error)
    ++rejected_;
  transport.enqueue(reply);
}

void DiagnosticClient::disconnect(uint32_t now, Transport &transport,
                                  const char *error) {
  state_ = LinkState::Offline;
  session_ = 0;
  waiting_ = false;
  pingRequested_ = false;
  retryAfter_ = now + 1000U;
  reconnectDelay_ = true;
  transport.clearPending();
  snprintf(lastError_, sizeof(lastError_), "%s", error);
}

bool DiagnosticClient::send(const char *name, uint32_t now,
                            Transport &transport) {
  if (waiting_ || nextId_ == 0)
    return false;
  Packet packet;
  packet.session = strcmp(name, "HELLO") == 0 ? 0 : session_;
  packet.id = nextId_;
  strcpy(packet.name, name);
  if (strcmp(name, "HELLO") == 0 || strcmp(name, "OPEN") == 0) {
    snprintf(packet.arguments, sizeof(packet.arguments), "nonce=%" PRIu32,
             session_);
  }
  if (!transport.enqueue(packet))
    return false;
  ++nextId_;
  pending_ = packet;
  waiting_ = true;
  retries_ = 0;
  sentAt_ = now;
  requestStartedAt_ = now;
  return true;
}

void DiagnosticClient::tick(uint32_t now, uint32_t freshToken,
                            Transport &transport) {
  if (state_ == LinkState::Offline) {
    if ((reconnectDelay_ && static_cast<int32_t>(now - retryAfter_) < 0) ||
        freshToken == 0)
      return;
    reconnectDelay_ = false;
    session_ = freshToken;
    nextId_ = 1;
    if (send("HELLO", now, transport))
      state_ = LinkState::Hello;
    return;
  }
  if (state_ == LinkState::Ready && now - lastResponseAt_ >= kLinkTimeoutMs) {
    ++timeouts_;
    disconnect(now, transport, "LINK_TIMEOUT");
    return;
  }
  if (waiting_) {
    if (now - requestStartedAt_ >= kLinkTimeoutMs) {
      ++timeouts_;
      disconnect(now, transport, "TX_OR_REPLY_TIMEOUT");
      return;
    }
    if (now - sentAt_ >= kRequestTimeoutMs) {
      if (retries_ == 2) {
        ++timeouts_;
        disconnect(now, transport, "REQUEST_TIMEOUT");
      } else if (transport.idle() && transport.enqueue(pending_)) {
        ++retries_;
        sentAt_ = now;
      }
    }
    return;
  }
  if (nextId_ == 0) {
    disconnect(now, transport, "ID_EXHAUSTED");
    return;
  }
  if (state_ == LinkState::Opening)
    send("OPEN", now, transport);
  else if (state_ == LinkState::Synchronizing)
    send("GET_STATUS", now, transport);
  else if (state_ == LinkState::Ready) {
    // Periodic STATUS has priority to avoid starvation when tapping PING.
    if (now - lastStatusAt_ >= kStatusPeriodMs)
      send("GET_STATUS", now, transport);
    else if (pingRequested_ || now - lastPingAt_ >= kPingPeriodMs) {
      if (send("PING", now, transport))
        pingRequested_ = false;
    }
  }
}

void DiagnosticClient::handle(const Packet &response, uint32_t now,
                              Transport &transport) {
  if (!waiting_ || response.id != pending_.id ||
      response.session != pending_.session ||
      strcmp(response.name, pending_.name) != 0) {
    ++unexpected_;
    return;
  }
  if (response.type == Type::Error) {
    disconnect(now, transport, response.arguments);
    return;
  }
  bool valid = false;
  if (state_ == LinkState::Hello && response.type == Type::Done) {
    char expected[kMaxArgumentBytes + 1];
    snprintf(expected, sizeof(expected),
             "version=1,nonce=%" PRIu32 ",role=MOTION,features=DIAG", session_);
    valid = strcmp(expected, response.arguments) == 0;
    if (valid)
      state_ = LinkState::Opening;
  } else if (state_ == LinkState::Opening && response.type == Type::Done) {
    valid = strcmp(response.arguments, "mode=DIAG") == 0;
    if (valid)
      state_ = LinkState::Synchronizing;
  } else if (strcmp(pending_.name, "GET_STATUS") == 0 &&
             response.type == Type::Status) {
    valid = parseSnapshot(response.arguments, motion_);
    if (valid) {
      lastStatusAt_ = now;
      state_ = LinkState::Ready;
    }
  } else if (strcmp(pending_.name, "PING") == 0 &&
             response.type == Type::Done) {
    valid = strcmp(response.arguments, "reply=PONG") == 0;
    if (valid) {
      lastPingAt_ = now;
      ++pongs_;
    }
  }
  if (!valid) {
    ++unexpected_;
    return;
  }
  // A retransmitted request has an ambiguous RTT; keep the last clean sample.
  if (retries_ == 0)
    rttMs_ = now - sentAt_;
  lastResponseAt_ = now;
  waiting_ = false;
  strcpy(lastError_, "NONE");
}

} // namespace protocol
} // namespace smart_mixer
