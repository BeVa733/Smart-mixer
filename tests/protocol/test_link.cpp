#include "DiagnosticLink.h"

#include <cstdlib>
#include <cstring>
#include <deque>
#include <iostream>
#include <string>

using namespace smart_mixer::protocol;
static unsigned checks = 0;
#define CHECK(c)                                                               \
  do {                                                                         \
    ++checks;                                                                  \
    if (!(c)) {                                                                \
      std::cerr << __LINE__ << ": " << #c << '\n';                             \
      std::exit(1);                                                            \
    }                                                                          \
  } while (false)

struct Wire {
  std::deque<char> bytes;
  int available() { return static_cast<int>(bytes.size()); }
  int read() {
    if (bytes.empty())
      return -1;
    unsigned char c = bytes.front();
    bytes.pop_front();
    return c;
  }
};
struct Writer {
  Wire &destination;
  bool enabled = true;
  bool dropping = false;
  size_t quota = 1;
  std::string sent;
  explicit Writer(Wire &wire) : destination(wire) {}
  size_t tryWrite(const char *bytes, size_t length) {
    if (!enabled)
      return 0;
    const size_t n = length < quota ? length : quota;
    sent.append(bytes, n);
    if (!dropping)
      for (size_t i = 0; i < n; ++i)
        destination.bytes.push_back(bytes[i]);
    return n;
  }
};

struct Bench {
  Transport clientTx, serverTx;
  DiagnosticClient client;
  DiagnosticServer server;
  Wire clientRx, serverRx;
  Writer clientWriter, serverWriter;
  MotionSnapshot snapshot;
  uint32_t now = 0;
  uint32_t token = 100;
  Bench() : clientWriter(serverRx), serverWriter(clientRx) {}
  void step() {
    client.tick(now, ++token, clientTx);
    clientTx.transmit(clientWriter);
    server.tick(now);
    serverTx.receive(serverRx, now, [this](const Packet &packet, uint32_t at) {
      server.handle(packet, at, snapshot, serverTx);
    });
    serverTx.transmit(serverWriter);
    clientTx.receive(clientRx, now, [this](const Packet &packet, uint32_t at) {
      client.handle(packet, at, clientTx);
    });
    ++now;
  }
  void run(uint32_t ms) {
    for (uint32_t i = 0; i < ms; ++i)
      step();
  }
  void connect() {
    for (unsigned i = 0; i < 5000 && client.state() != LinkState::Ready; ++i)
      step();
    CHECK(client.state() == LinkState::Ready);
    CHECK(client.session() == server.session());
  }
};

static void exchangeAndResets() {
  Bench b;
  b.snapshot.upperLimit = true;
  b.snapshot.position = INT32_MIN;
  b.connect();
  CHECK(!b.client.motion().homed);
  CHECK(b.client.motion().upperLimit);
  CHECK(b.client.motion().position == INT32_MIN);
  b.snapshot.position = INT32_MAX;
  b.run(2500);
  CHECK(b.client.pongs() > 0);
  CHECK(b.client.motion().position == INT32_MAX);
  CHECK(b.client.timeouts() == 0);
  CHECK(b.client.requestPing());
  const uint32_t pongs = b.client.pongs();
  b.run(600);
  CHECK(b.client.pongs() > pongs);
  // Arduino reset invalidates the old session; client must discover this and
  // negotiate a fresh token rather than silently accepting the old session.
  const uint32_t oldSession = b.client.session();
  b.server = DiagnosticServer();
  b.serverTx.clearPending();
  b.run(4500);
  CHECK(b.client.state() == LinkState::Ready);
  CHECK(b.client.session() != oldSession);
  CHECK(b.client.session() == b.server.session());
  // ESP32 reset while Arduino's lease is still alive. HELLO gets BUSY until
  // that lease expires, then the client automatically establishes a new one.
  const uint32_t beforeReset = b.server.session();
  b.client = DiagnosticClient();
  b.clientTx.clearPending();
  b.clientRx.bytes.clear();
  b.serverRx.bytes.clear();
  b.run(5500);
  CHECK(b.client.state() == LinkState::Ready);
  CHECK(b.client.session() != beforeReset);
}

static void lossRetryAndRecovery() {
  Bench b;
  // Lose the entire first HELLO response, but allow same-ID retry to succeed.
  b.serverWriter.dropping = true;
  b.run(400);
  b.serverWriter.dropping = false;
  b.connect();
  CHECK(b.clientWriter.sent.find("|1|CMD|HELLO|") != std::string::npos);
  CHECK(b.clientWriter.sent.find(
            "|1|CMD|HELLO|", b.clientWriter.sent.find("|1|CMD|HELLO|") + 1) !=
        std::string::npos);
  const uint32_t old = b.client.session();
  b.serverWriter.dropping = true;
  b.run(3000);
  CHECK(b.client.state() != LinkState::Ready);
  CHECK(b.client.timeouts() > 0);
  b.serverWriter.dropping = false;
  b.run(5000);
  CHECK(b.client.state() == LinkState::Ready);
  CHECK(b.client.session() != old);
  b.clientWriter.dropping = true;
  b.run(2500);
  CHECK(b.server.session() == 0);
  CHECK(b.client.state() != LinkState::Ready);
  b.clientWriter.dropping = false;
  b.run(5000);
  CHECK(b.client.state() == LinkState::Ready);

  // Completely blocked TX must also time out during HELLO/OPEN, not hang.
  Bench blocked;
  blocked.clientWriter.enabled = false;
  blocked.run(2500);
  CHECK(blocked.client.timeouts() > 0);
  CHECK(blocked.client.state() == LinkState::Offline);
  blocked.clientWriter.enabled = true;
  blocked.run(5000);
  CHECK(blocked.client.state() == LinkState::Ready);
}

static Packet command(uint32_t session, uint32_t id, const char *name,
                      const char *args = "") {
  Packet packet;
  packet.session = session;
  packet.id = id;
  std::strcpy(packet.name, name);
  std::strcpy(packet.arguments, args);
  return packet;
}

static Packet drain(Transport &transport) {
  Wire wire;
  Writer writer(wire);
  writer.quota = 7;
  while (!transport.idle())
    transport.transmit(writer);
  Parser parser;
  Packet reply;
  unsigned frames = 0;
  while (wire.available()) {
    if (parser.feed(static_cast<char>(wire.read()), 0, reply) == Result::Frame)
      ++frames;
  }
  CHECK(frames == 1);
  return reply;
}

static void validationAndQueue() {
  DiagnosticServer server;
  Transport transport;
  MotionSnapshot snapshot;
  server.handle(command(0, 1, "HELLO", "nonce=77"), 0, snapshot, transport);
  CHECK(drain(transport).type == Type::Done);
  server.handle(command(77, 2, "OPEN", "nonce=78"), 10, snapshot, transport);
  CHECK(std::strcmp(drain(transport).arguments, "code=WRONG_SESSION") == 0);
  server.handle(command(77, 2, "OPEN", "nonce=77"), 20, snapshot, transport);
  CHECK(drain(transport).type == Type::Done);
  // Repeating OPEN is harmless if its first response was lost.
  server.handle(command(77, 2, "OPEN", "nonce=77"), 30, snapshot, transport);
  CHECK(drain(transport).type == Type::Done);
  const char *forbidden[] = {"DOSE", "MOVE", "HOME", "PRIME", "STOP", "HCAL"};
  for (const char *name : forbidden) {
    server.handle(command(77, 3, name), 40, snapshot, transport);
    CHECK(std::strcmp(drain(transport).arguments, "code=UNSUPPORTED_COMMAND") ==
          0);
  }
  server.handle(command(76, 4, "PING"), 50, snapshot, transport);
  CHECK(std::strcmp(drain(transport).arguments, "code=WRONG_SESSION") == 0);
  server.handle(command(77, 5, "PING", "x=1"), 50, snapshot, transport);
  CHECK(std::strcmp(drain(transport).arguments, "code=BAD_ARGUMENTS") == 0);
  server.handle(command(0, 6, "HELLO", "nonce=88"), 100, snapshot, transport);
  CHECK(std::strcmp(drain(transport).arguments, "code=BUSY") == 0);
  server.tick(2030); // Rejected traffic does not keep a session alive.
  CHECK(server.session() == 0);

  for (unsigned i = 0; i < 4; ++i)
    CHECK(transport.enqueue(command(0, i + 1, "PING")));
  CHECK(!transport.enqueue(command(0, 5, "PING")));
  CHECK(transport.dropped() == 1);
  transport.clearPending();
  CHECK(transport.idle());
  CHECK(transport.dropped() == 1);
}

static void staleResponsesAndRollover() {
  Bench b;
  b.now = UINT32_MAX - 1500;
  b.connect();
  b.run(4000);
  CHECK(b.client.state() == LinkState::Ready);
  CHECK(b.client.timeouts() == 0);
  const uint32_t pongs = b.client.pongs();
  Packet stale = command(b.client.session() + 1, 1, "PING");
  stale.type = Type::Done;
  std::strcpy(stale.arguments, "reply=PONG");
  b.client.handle(stale, b.now, b.clientTx);
  CHECK(b.client.pongs() == pongs);
  CHECK(b.client.unexpected() > 0);
  // Wrong version response content with matching HELLO header cannot open UI.
  DiagnosticClient client;
  Transport tx;
  client.tick(0, 12, tx);
  Packet hello =
      command(0, 1, "HELLO", "version=2,nonce=12,role=MOTION,features=DIAG");
  hello.type = Type::Done;
  client.handle(hello, 1, tx);
  CHECK(client.state() == LinkState::Hello);
  CHECK(client.unexpected() == 1);
  // Correct header but mismatched nonce must also be ignored.
  std::strcpy(hello.arguments, "version=1,nonce=13,role=MOTION,features=DIAG");
  client.handle(hello, 2, tx);
  CHECK(client.state() == LinkState::Hello);
  std::strcpy(hello.arguments, "version=1,nonce=12,role=MOTION,features=DIAG");
  client.handle(hello, 3, tx);
  CHECK(client.state() == LinkState::Opening);
  client.tick(4, 99, tx);
  Packet opened = command(12, 2, "OPEN", "mode=DIAG");
  opened.type = Type::Done;
  client.handle(opened, 5, tx);
  client.tick(6, 99, tx);
  CHECK(client.state() == LinkState::Synchronizing);
  const char *badStates[] = {"homed=2,limit=0,p1=0,p2=0,pos=0",
                             "homed=0,limit=0,p1=0,p2=0,pos=2147483648",
                             "homed=0,limit=0,p1=0,p2=0,pos=-2147483649",
                             "homed=0,limit=0,p1=0,p2=0,pos=0,extra=1",
                             "homed=0,limit=0,p1=0,p2=0,pos=",
                             "homed=0,limit=0,p1=0,pos=0"};
  for (const char *bad : badStates) {
    Packet status = command(12, 3, "GET_STATUS", bad);
    status.type = Type::Status;
    client.handle(status, 7, tx);
    CHECK(client.state() == LinkState::Synchronizing);
    CHECK(!client.motion().homed);
  }
  Packet status =
      command(12, 3, "GET_STATUS", "homed=1,limit=1,p1=0,p2=0,pos=0");
  status.type = Type::Status;
  client.handle(status, 8, tx);
  CHECK(client.state() == LinkState::Ready);
  CHECK(client.motion().homed);
  // A valid old response with the wrong ID/session cannot overwrite the model.
  status.id = 999;
  std::strcpy(status.arguments, "homed=0,limit=0,p1=0,p2=0,pos=100");
  client.handle(status, 9, tx);
  CHECK(client.motion().homed);
}

int main() {
  exchangeAndResets();
  lossRetryAndRecovery();
  validationAndQueue();
  staleResponsesAndRollover();
  std::cout << checks << " link checks passed\n";
}
