#include "diagnostic_uart.hpp"
#include <Arduino.h>
#include <DiagnosticLink.h>

// Dedicated read-only bench firmware. No EEPROM writes, homing, stepping,
// pump commands or legacy USB command dispatcher are compiled into this image.
namespace {
using namespace smart_mixer::protocol;
Transport transport;
DiagnosticServer server;
uint32_t nextUsbReport = 0;
DiagnosticUart uart;
} // namespace

void setup() {
  // No motor holding load may be attached to this bench image: EN disables it.
  digitalWrite(8, LOW);
  digitalWrite(10, LOW);
  digitalWrite(7, LOW);
  digitalWrite(5, LOW);
  digitalWrite(6, HIGH);
  pinMode(8, OUTPUT);
  pinMode(10, OUTPUT);
  pinMode(7, OUTPUT);
  pinMode(5, OUTPUT);
  pinMode(6, OUTPUT);
  pinMode(9, INPUT_PULLUP);
  pinMode(LED_BUILTIN, OUTPUT);
  Serial.begin(115200);
  uart.begin(kLinkBaud);
}

void loop() {
  const uint32_t now = millis();
  server.tick(now);
  transport.receive(uart, now, [](const Packet &packet, uint32_t receivedAt) {
    MotionSnapshot snapshot;
    snapshot.upperLimit = digitalRead(9) == HIGH;
    server.handle(packet, receivedAt, snapshot, transport);
  });
  transport.transmit(uart);
  digitalWrite(LED_BUILTIN, server.session() != 0 ? HIGH : LOW);

  // An absent/slow USB host must not delay UART polling.
  if (static_cast<int32_t>(now - nextUsbReport) >= 0) {
    nextUsbReport = now + 2000;
    char report[96];
    const int length = snprintf(
        report, sizeof(report),
        "DIAG session=%lu rx=%lu tx=%lu crc=%lu drop=%lu uart_err=%lu\n",
        static_cast<unsigned long>(server.session()),
        static_cast<unsigned long>(transport.received().frames),
        static_cast<unsigned long>(transport.sent()),
        static_cast<unsigned long>(transport.received().invalidChecksum),
        static_cast<unsigned long>(transport.dropped()),
        static_cast<unsigned long>(uart.errors()));
    if (length > 0 && length < static_cast<int>(sizeof(report)) && Serial &&
        Serial.availableForWrite() >= length) {
      Serial.write(reinterpret_cast<const uint8_t *>(report),
                   static_cast<size_t>(length));
    }
  }
}
