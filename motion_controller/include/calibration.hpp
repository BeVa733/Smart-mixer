#pragma once
#include <Arduino.h>
#include <EEPROM.h>

class PumpCalibration {
public:
  static constexpr uint32_t MAGIC = 0x534D4932UL;
  static constexpr int EEPROM_ADDRESS = 0;
  static constexpr uint8_t MAX_HEIGHT_POINTS = 20;
  struct HeightPoint { uint16_t volumeMl = 0; int32_t steps = 0; };
  float pump1MlPerSecond = 0.0f, pump2MlPerSecond = 0.0f;
  bool begin() {
    heightCount_ = 0; conflict_ = false;
    pump1MlPerSecond = pump2MlPerSecond = 0;
    Record current; EEPROM.get(EEPROM_ADDRESS, current);
    if(current.magic == MAGIC && validRates(current.rate1, current.rate2)) {
      pump1MlPerSecond = current.rate1; pump2MlPerSecond = current.rate2;
      if(validCurve(current.count, current.points)) copyCurve(current.count, current.points);
      return true;
    }
    Legacy old; EEPROM.get(EEPROM_ADDRESS, old);
    if(old.magic != 0x534D4958UL || !validRates(old.rate1, old.rate2)) return false;
    pump1MlPerSecond = old.rate1; pump2MlPerSecond = old.rate2;
    bool a = old.count1 && validCurve(old.count1, old.points1);
    bool b = old.count2 && validCurve(old.count2, old.points2);
    if(a && b) {
      bool same = old.count1 == old.count2;
      for(unsigned i = 0; same && i < old.count1; ++i)
        same = old.points1[i].volumeMl == old.points2[i].volumeMl && old.points1[i].steps == old.points2[i].steps;
      if(same) copyCurve(old.count1, old.points1); else conflict_ = true;
    } else if(a) copyCurve(old.count1, old.points1);
    else if(b) copyCurve(old.count2, old.points2);
    // Boot migrates in RAM only; two different old curves require recalibration.
    return true;
  }
  bool heightMigrationConflict() const { return conflict_; }
  void setRate(uint8_t pump, float rate) {
    if(pump == 1) pump1MlPerSecond = rate;
    else if(pump == 2) pump2MlPerSecond = rate;
    else return;
    save();
  }
  void clearHeight() {
    heightCount_ = 0; conflict_ = false;
    for(auto &point : height_) point = HeightPoint{};
    save();
  }
  bool addHeightPoint(uint16_t volume, int32_t steps) {
    if(heightCount_ >= MAX_HEIGHT_POINTS || !volume || steps > 0 ||
       (heightCount_ && (volume <= height_[heightCount_-1].volumeMl || steps < height_[heightCount_-1].steps))) return false;
    height_[heightCount_].volumeMl = volume; height_[heightCount_].steps = steps;
    ++heightCount_; conflict_ = false; save(); return true;
  }
  uint8_t heightPointCount() const { return heightCount_; }
  const HeightPoint *heightPoints() const { return height_; }
private:
  struct Record {
    float rate1 = 0, rate2 = 0;
    uint32_t magic = MAGIC;
    uint8_t count = 0;
    HeightPoint points[MAX_HEIGHT_POINTS]{};
  };
  // Exact old field order/alignment, only for reading existing EEPROM.
  struct Legacy {
    float rate1 = 0, rate2 = 0;
    uint32_t magic = 0;
    uint8_t count1 = 0, count2 = 0;
    HeightPoint points1[MAX_HEIGHT_POINTS]{}, points2[MAX_HEIGHT_POINTS]{};
  };
  uint8_t heightCount_ = 0;
  HeightPoint height_[MAX_HEIGHT_POINTS]{};
  bool conflict_ = false;
  static bool validRates(float a, float b) { return (a >= 0.1f && a <= 20) || (b >= 0.1f && b <= 20); }
  static bool validCurve(unsigned count, const HeightPoint *points) {
    if(count > MAX_HEIGHT_POINTS) return false;
    for(unsigned i = 0; i < count; ++i) {
      if(!points[i].volumeMl || points[i].steps > 0 ||
         (i && (points[i].volumeMl <= points[i-1].volumeMl || points[i].steps < points[i-1].steps))) return false;
    }
    return true;
  }
  void copyCurve(unsigned count, const HeightPoint *points) {
    heightCount_ = count;
    for(unsigned i = 0; i < count; ++i) height_[i] = points[i];
  }
  void save() {
    Record record; record.rate1 = pump1MlPerSecond; record.rate2 = pump2MlPerSecond;
    record.count = heightCount_;
    for(unsigned i = 0; i < heightCount_; ++i) record.points[i] = height_[i];
    EEPROM.put(EEPROM_ADDRESS, record);
  }
};
