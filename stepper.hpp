#pragma once

#include <Arduino.h>

class StepperMotor {
private:
  static constexpr uint8_t STEP = 4;
  static constexpr uint8_t DIR = 5;
  static constexpr uint8_t ENABLE = 6;

public:
  void begin() {
    pinMode(STEP, OUTPUT);
    pinMode(DIR, OUTPUT);
    pinMode(ENABLE, OUTPUT);

    digitalWrite(STEP, LOW);
    digitalWrite(DIR, LOW);
    digitalWrite(ENABLE, LOW);
  }

  void step(int8_t direction) {
    digitalWrite(DIR, direction > 0 ? HIGH : LOW);
    delayMicroseconds(10);

    digitalWrite(STEP, HIGH);
    delayMicroseconds(500);
    digitalWrite(STEP, LOW);
    delayMicroseconds(500);
  }
};
