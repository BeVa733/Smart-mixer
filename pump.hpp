#pragma once

#include <Arduino.h>

class Pump {
private:
  static constexpr uint8_t CONTROL = 7;

public:
  void begin() {
    pinMode(CONTROL, OUTPUT);
    stop();
  }

  void setEnabled(bool enabled) { digitalWrite(CONTROL, enabled ? HIGH : LOW); }

  void stop() { setEnabled(false); }
};
