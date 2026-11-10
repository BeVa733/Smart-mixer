#pragma once

#include <Arduino.h>
#include <EEPROM.h>

struct PumpCalibration {
  static constexpr uint32_t MAGIC = 0x534D4958UL;
  static constexpr int EEPROM_ADDRESS = 0;
  static constexpr uint8_t MAX_HEIGHT_POINTS = 20;

  struct HeightPoint {
    uint16_t volumeMl = 0;
    int32_t steps = 0;
  };

  float pump1MlPerSecond = 0.0f;
  float pump2MlPerSecond = 0.0f;

  // Keep magic directly after the two old fields: this preserves the EEPROM
  // layout used by the first version of the project.
  uint32_t magic = MAGIC;

  uint8_t pump1HeightCount = 0;
  uint8_t pump2HeightCount = 0;
  HeightPoint pump1Height[MAX_HEIGHT_POINTS] = {};
  HeightPoint pump2Height[MAX_HEIGHT_POINTS] = {};

  bool begin() {
    PumpCalibration stored;
    EEPROM.get(EEPROM_ADDRESS, stored);

    if (stored.isValid()) {
      *this = stored;
      // Bytes after the old calibration record can be 0xFF in EEPROM.
      // Do not treat that as a valid number of height points.
      if (pump1HeightCount > MAX_HEIGHT_POINTS) {
        pump1HeightCount = 0;
      }
      if (pump2HeightCount > MAX_HEIGHT_POINTS) {
        pump2HeightCount = 0;
      }
      return true;
    } else {
      magic = 0;
      pump1HeightCount = 0;
      pump2HeightCount = 0;
      return false;
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

  void clearHeight(uint8_t pumpNumber) {
    HeightPoint *points = mutableHeightPoints(pumpNumber);
    uint8_t &count = mutableHeightPointCount(pumpNumber);
    if (points == nullptr) {
      return;
    }

    count = 0;
    for (uint8_t i = 0; i < MAX_HEIGHT_POINTS; ++i) {
      points[i] = HeightPoint{};
    }
    save();
  }

  bool addHeightPoint(uint8_t pumpNumber, uint16_t volumeMl,
                      int32_t steps) {
    HeightPoint *points = mutableHeightPoints(pumpNumber);
    uint8_t &count = mutableHeightPointCount(pumpNumber);
    if (points == nullptr || count >= MAX_HEIGHT_POINTS) {
      return false;
    }

    points[count].volumeMl = volumeMl;
    points[count].steps = steps;
    ++count;
    save();
    return true;
  }

  uint8_t heightPointCount(uint8_t pumpNumber) const {
    return pumpNumber == 1 ? pump1HeightCount
                           : (pumpNumber == 2 ? pump2HeightCount : 0);
  }

  const HeightPoint *heightPoints(uint8_t pumpNumber) const {
    return pumpNumber == 1 ? pump1Height
                           : (pumpNumber == 2 ? pump2Height : nullptr);
  }

private:
  bool isValid() const {
    return magic == MAGIC && (validRate(pump1MlPerSecond) ||
                              validRate(pump2MlPerSecond));
  }

  static bool validRate(float rate) {
    return rate >= 0.1f && rate <= 20.0f;
  }

  void save() {
    magic = MAGIC;
    EEPROM.put(EEPROM_ADDRESS, *this);
  }

  HeightPoint *mutableHeightPoints(uint8_t pumpNumber) {
    return pumpNumber == 1 ? pump1Height
                           : (pumpNumber == 2 ? pump2Height : nullptr);
  }

  uint8_t &mutableHeightPointCount(uint8_t pumpNumber) {
    static uint8_t invalidCount = 0;
    return pumpNumber == 1 ? pump1HeightCount
                           : (pumpNumber == 2 ? pump2HeightCount
                                              : invalidCount);
  }
};
