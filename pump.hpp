#pragma once

#include <Arduino.h>

class Pump {
private:
  uint8_t controlPin;

public:
  explicit Pump(uint8_t pin) : controlPin(pin) {}

  void begin() {
    pinMode(controlPin, OUTPUT);
    stop();
  }

  void setEnabled(bool enabled) {
    digitalWrite(controlPin, enabled ? HIGH : LOW);
  }

  void stop() { setEnabled(false); }
};
