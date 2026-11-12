#pragma once
#include <stdint.h>

namespace smart_mixer {
inline bool parseRecipeValues(const char *text, uint32_t *values, const uint32_t *limits, unsigned count) {
  for(unsigned i = 0; i < count; ++i) {
    if(*text < '0' || *text > '9') return false;
    uint32_t v = 0;
    while(*text >= '0' && *text <= '9') {
      uint32_t digit = *text++ - '0';
      if(digit > limits[i] || v > (limits[i] - digit) / 10) return false;
      v = v * 10 + digit;
    }
    values[i] = v;
    if(i + 1 < count) { if(*text != ' ') return false; ++text; }
  }
  return *text == 0;
}
struct HeightSample { uint16_t ml; int32_t steps; };
enum class DrinkPhase { Idle, Positioning, Lowering, FirstLayer, SecondLayer, Returning, Done, Cancelled, Error };
class RecipeExecution {
public:
  // Same lower travel limit as MAX_DOWNWARD_MICROSTEPS in motion_controller/include/calibration.hpp.
  static constexpr int32_t MAX_DOWNWARD_STEPS = 1000;
  // Layered mode: the shaft stays this many microsteps above the calculated liquid level
  // (requirement: lead the layer by no more than 20-30 microsteps).
  static constexpr int32_t LEVEL_LEAD_STEPS = 25;
  bool start(uint32_t id, uint16_t ml1, uint16_t ml2, uint32_t ms1, uint32_t ms2,
             const HeightSample *curve, unsigned count, uint32_t now, uint8_t firstPump = 1,
             bool layered = true) {
    if(firstPump != 1 && firstPump != 2) return false;
    // Inputs belong to physical pumps. Internally volumes/times follow layer order.
    if(firstPump == 2) {
      uint16_t volume = ml1; ml1 = ml2; ml2 = volume;
      uint32_t duration = ms1; ms1 = ms2; ms2 = duration;
    }
    // Layered mode lowers the shaft to the first layer, so that layer must be on the curve.
    // Mixed mode pours everything at the rim; the curve still defines the rim and capacity.
    if(active() || id == 0 || id == lastId_ || !curve || count < 2 || count > 20 ||
       ml1 > 1000 || ml2 > 1000 || ml1 + ml2 == 0 || (layered && ml1 && ml2 && ml1 < curve[0].ml) ||
       ml1 + ml2 > curve[count-1].ml || (ml1 && (!ms1 || ms1 > 1800000)) ||
       (ml2 && (!ms2 || ms2 > 1800000))) return false;
    for(unsigned i = 0; i < count; ++i) {
      if(curve[i].ml == 0 || curve[i].steps < -MAX_DOWNWARD_STEPS || curve[i].steps > 0 ||
         (i && (curve[i].ml <= curve[i-1].ml || curve[i].steps < curve[i-1].steps))) return false;
    }
    for(unsigned i = 0; i < count; ++i) curve_[i] = curve[i];
    count_ = count; ml1_ = ml1; ml2_ = ml2; ms1_ = ms1; ms2_ = ms2;
    firstPump_ = firstPump; layered_ = layered;
    id_ = lastId_ = id; stageAt_ = heartbeatAt_ = now;
    error_ = "NONE"; phase_ = DrinkPhase::Positioning; percent_ = 0;
    return true;
  }
  bool active() const { return phase_ == DrinkPhase::Positioning || phase_ == DrinkPhase::Lowering || phase_ == DrinkPhase::FirstLayer ||
    phase_ == DrinkPhase::SecondLayer || phase_ == DrinkPhase::Returning; }
  void heartbeat(uint32_t id, uint32_t now) { if(active() && id == id_) heartbeatAt_ = now; }
  void cancel() { if(active()) phase_ = DrinkPhase::Cancelled; }
  void fail(const char *error) { error_ = error; phase_ = DrinkPhase::Error; }
  int pump() const { return phase_ == DrinkPhase::FirstLayer ? firstPump_ : phase_ == DrinkPhase::SecondLayer ? 3 - firstPump_ : 0; }
  uint32_t id() const { return id_; }
  bool layered() const { return layered_; }
  DrinkPhase phase() const { return phase_; }
  int percent() const { return percent_; }
  const char *error() const { return error_; }
  // The caller emits at most ONE physical microstep per UI/serial polling loop.
  int step(uint32_t now, int32_t position, bool upperLimit) {
    if(!active()) return 0;
    if(now - heartbeatAt_ >= 2000) { fail("LINK_TIMEOUT"); return 0; }
    if(position < -MAX_DOWNWARD_STEPS || position > 0) { fail("POSITION"); return 0; }
    const int32_t rim = curve_[count_-1].steps;
    int32_t target = rim;
    if(phase_ == DrinkPhase::Positioning) {
      if(now - stageAt_ >= 30000) { fail("MOVE_TIMEOUT"); return 0; }
      if(position == target) { phase_ = ml1_ ? DrinkPhase::FirstLayer : DrinkPhase::SecondLayer; stageAt_ = now; }
    }
    if(phase_ == DrinkPhase::FirstLayer) {
      uint32_t elapsed = now - stageAt_;
      if(elapsed >= ms1_) {
        percent_ = ml1_ * 100U / (ml1_ + ml2_);
        phase_ = !ml2_ ? DrinkPhase::Returning : layered_ ? DrinkPhase::Lowering : DrinkPhase::SecondLayer;
        stageAt_ = now;
      }
      else percent_ = static_cast<int>(static_cast<uint64_t>(ml1_) * elapsed * 100U / ms1_ / (ml1_ + ml2_));
    }
    if(phase_ == DrinkPhase::Lowering) {
      if(now - stageAt_ >= 30000) { fail("MOVE_TIMEOUT"); return 0; }
      target = aboveLevel(ml1_ * 1000U);
      if(position == target) { phase_ = DrinkPhase::SecondLayer; stageAt_ = now; }
    }
    if(phase_ == DrinkPhase::SecondLayer) {
      uint32_t elapsed = now - stageAt_;
      if(elapsed >= ms2_) { phase_ = DrinkPhase::Returning; stageAt_ = now; }
      else {
        uint32_t volume1000 = ml1_ * 1000U + static_cast<uint64_t>(ml2_) * elapsed * 1000U / ms2_;
        percent_ = static_cast<int>(volume1000 / (10U * (ml1_ + ml2_)));
        target = ml1_ && layered_ ? aboveLevel(volume1000) : rim;
      }
    }
    if(phase_ == DrinkPhase::Returning) {
      percent_ = 100;
      if(position == rim) {
        if(rim == 0 && !upperLimit) fail("UPPER_LIMIT"); else phase_ = DrinkPhase::Done;
        return 0;
      }
      if(now - stageAt_ >= 30000) { fail("MOVE_TIMEOUT"); return 0; }
      target = rim;
    }
    if(phase_ == DrinkPhase::Lowering && position < -5 && upperLimit) { fail("LIMIT_STUCK"); return 0; }
    if(target > position && upperLimit) { fail("UNEXPECTED_LIMIT"); return 0; }
    return target > position ? 1 : target < position ? -1 : 0;
  }
private:
  int32_t aboveLevel(uint32_t volume1000) const {
    int32_t target = height(volume1000) + LEVEL_LEAD_STEPS;
    const int32_t rim = curve_[count_-1].steps;
    return target > rim ? rim : target;
  }
  int32_t height(uint32_t volume1000) const {
    if(volume1000 <= curve_[0].ml * 1000U) return curve_[0].steps;
    for(unsigned i = 1; i < count_; ++i) {
      if(volume1000 <= curve_[i].ml * 1000U) {
        uint32_t offset = volume1000 - curve_[i-1].ml * 1000U;
        return curve_[i-1].steps + static_cast<int64_t>(curve_[i].steps - curve_[i-1].steps) * offset /
          ((curve_[i].ml - curve_[i-1].ml) * 1000U);
      }
    }
    return curve_[count_-1].steps;
  }
  HeightSample curve_[20]{};
  unsigned count_ = 0;
  uint8_t firstPump_ = 1;
  bool layered_ = true;
  uint16_t ml1_ = 0, ml2_ = 0;
  uint32_t ms1_ = 0, ms2_ = 0, id_ = 0, lastId_ = 0, stageAt_ = 0, heartbeatAt_ = 0;
  DrinkPhase phase_ = DrinkPhase::Idle;
  int percent_ = 0;
  const char *error_ = "NONE";
};
}
