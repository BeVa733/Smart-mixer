#pragma once

#include <Arduino.h>
#include <EEPROM.h>

struct PumpCalibration {
  static constexpr uint32_t MAGIC = 0x534D4958UL;
  static constexpr int EEPROM_ADDRESS = 0;

  float pump1MlPerSecond = 1.364f;
  float pump2MlPerSecond = 1.197f;

  void begin() {
    PumpCalibration stored;
    EEPROM.get(EEPROM_ADDRESS, stored);

    if (stored.isValid()) {
      *this = stored;
    } else {
      save();
    }
  }

  void setRate(uint8_t pumpNumber, float rate) {
    if (pumpNumber == 1) {
      pump1MlPerSecond = rate;
    } else if (pumpNumber == 2) {
      pump2MlPerSecond = rate;
    }

    save();
  }

private:
  bool isValid() const {
    return magic == MAGIC && validRate(pump1MlPerSecond) &&
           validRate(pump2MlPerSecond);
  }

  static bool validRate(float rate) {
    return rate >= 0.1f && rate <= 20.0f;
  }

  void save() {
    magic = MAGIC;
    EEPROM.put(EEPROM_ADDRESS, *this);
  }

  uint32_t magic = MAGIC;
};
