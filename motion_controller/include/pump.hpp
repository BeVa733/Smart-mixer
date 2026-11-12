#pragma once

#include <Arduino.h>

class Pump {
private:
  uint8_t controlPin;
  float mlPerSecond = 0.0f;
  bool dosing = false;
  unsigned long doseStartedAt = 0;
  unsigned long doseDurationMs = 0;

public:
  explicit Pump(uint8_t pin) : controlPin(pin) {}

  void begin() {
    pinMode(controlPin, OUTPUT);
    stop();
  }

  void setEnabled(bool enabled) {
    digitalWrite(controlPin, enabled ? HIGH : LOW);
  }

  // 0 (or less) = not calibrated: dosing by volume is refused.
  void setFlowRate(float flowMlPerSecond) {
    mlPerSecond = flowMlPerSecond > 0 ? flowMlPerSecond : 0.0f;
  }

  void stop() {
    dosing = false;
    setEnabled(false);
  }

  unsigned long msForVolume(float volumeMl) const {
    if (mlPerSecond <= 0 || volumeMl <= 0) {
      return 0;
    }
    return static_cast<unsigned long>(volumeMl / mlPerSecond * 1000.0f);
  }

  void startDose(float volumeMl) {
    doseDurationMs = msForVolume(volumeMl);
    if (doseDurationMs == 0) {
      return;
    }

    doseStartedAt = millis();
    dosing = true;
    setEnabled(true);
  }

  // Run for a fixed time, independent of the flow-rate calibration.
  void startFor(unsigned long durationMs) {
    if (durationMs == 0) {
      return;
    }
    doseDurationMs = durationMs;
    doseStartedAt = millis();
    dosing = true;
    setEnabled(true);
  }

  bool update() {
    if (dosing && millis() - doseStartedAt >= doseDurationMs) {
      stop();
      return true;
    }
    return false;
  }

  bool isDosing() const { return dosing; }
  float flowRate() const { return mlPerSecond; }
};
