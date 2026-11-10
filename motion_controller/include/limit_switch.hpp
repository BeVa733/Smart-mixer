#pragma once

#include <Arduino.h>

class LimitSwitch {
private:
  static constexpr uint8_t PIN = 9;

public:
  void begin() { pinMode(PIN, INPUT_PULLUP); }

  // Схема подключения: COM - GND, NC - D9.
  bool isTriggered() const { return digitalRead(PIN) == HIGH; }
};
