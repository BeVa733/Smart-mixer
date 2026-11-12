#pragma once

#include <Arduino.h>

class StepperMotor {
private:
  // BIGTREETECH TMC2209 V1.3 in standalone STEP/DIR mode.
  static constexpr uint8_t STEP_PIN = 7;
  static constexpr uint8_t DIR_PIN = 5;
  static constexpr uint8_t ENABLE_PIN = 6;
  static constexpr unsigned int STEP_DELAY_US = 500;
  static constexpr unsigned int SLOW_STEP_DELAY_US = 1000;

public:
  void begin() {
    pinMode(STEP_PIN, OUTPUT);
    pinMode(DIR_PIN, OUTPUT);
    pinMode(ENABLE_PIN, OUTPUT);

    digitalWrite(STEP_PIN, LOW);
    digitalWrite(DIR_PIN, LOW);
    setEnabled(true);
  }

  // For the TMC2209 itself, MS1=GND and MS2=GND means 1/8 microstepping.
  // Each STEP pulse below is therefore one 1/8 microstep.
  static constexpr uint8_t MICROSTEPS = 8;

  void setEnabled(bool enabled) {
    // TMC2209 EN is active-low: LOW enables the motor outputs.
    digitalWrite(ENABLE_PIN, enabled ? LOW : HIGH);
  }

  void step(int8_t direction) { step(direction, STEP_DELAY_US); }

  void stepSlow(int8_t direction) { step(direction, SLOW_STEP_DELAY_US); }

private:
  void step(int8_t direction, unsigned int pulseDelayMicros) {
    digitalWrite(DIR_PIN, direction > 0 ? HIGH : LOW);
    delayMicroseconds(10);

    digitalWrite(STEP_PIN, HIGH);
    delayMicroseconds(pulseDelayMicros);
    digitalWrite(STEP_PIN, LOW);
    delayMicroseconds(pulseDelayMicros);
  }
};
