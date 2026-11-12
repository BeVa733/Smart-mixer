#include "calibration.hpp"
#include <cmath>
#include <cstdio>
#include <cstddef>
#include <stdexcept>
TestEeprom EEPROM;
void check(bool value, const char *message) { if(!value) throw std::runtime_error(message); }
struct Legacy {
  float rate1 = 1.364f, rate2 = 1.197f;
  uint32_t magic = 0x534D4958UL;
  uint8_t count1 = 0, count2 = 0;
  PumpCalibration::HeightPoint points1[20]{}, points2[20]{};
};
void oldCurve(PumpCalibration::HeightPoint *p) {
  p[0].volumeMl = 5; p[0].steps = -100;
  p[1].volumeMl = 50; p[1].steps = -10;
}
int main() {
  static_assert(offsetof(Legacy, magic) == 8 && offsetof(Legacy, points1) == 16 && sizeof(Legacy) == 336,
                "Legacy EEPROM layout must match UNO R4");
  PumpCalibration empty; check(!empty.begin() && EEPROM.writes == 0, "empty EEPROM does not cause writes");
  for(unsigned source = 1; source <= 2; ++source) {
    EEPROM.reset(); Legacy old;
    if(source == 1) { old.count1 = 2; oldCurve(old.points1); }
    else { old.count2 = 2; oldCurve(old.points2); }
    EEPROM.put(0, old); EEPROM.writes = 0;
    PumpCalibration calibration;
    check(calibration.begin() && calibration.heightPointCount() == 2, "single legacy table preserved regardless of pump");
    check(calibration.heightPoints()[1].steps == -10 && !calibration.heightMigrationConflict(), "common height content");
    check(std::fabs(calibration.pump1MlPerSecond - 1.364f) < 0.001f &&
          std::fabs(calibration.pump2MlPerSecond - 1.197f) < 0.001f, "independent pump rates preserved");
    check(EEPROM.writes == 0, "migration must not write EEPROM at boot");
    calibration.setRate(2, 1.5f);
    PumpCalibration reloaded; check(reloaded.begin() && reloaded.heightPointCount() == 2 && reloaded.pump2MlPerSecond == 1.5f,
                                   "explicit save uses new format without losing common table");
    reloaded.clearHeight(); check(reloaded.heightPointCount() == 0 && reloaded.pump2MlPerSecond == 1.5f, "clear levels preserves pump rates");
    check(reloaded.addHeightPoint(5, -100) && reloaded.addHeightPoint(10, -90), "shared table accepts ordered samples");
    check(!reloaded.addHeightPoint(15, -110) && !reloaded.addHeightPoint(10, -80) && !reloaded.addHeightPoint(20, -651), "invalid height samples do not persist");
  }
  EEPROM.reset(); Legacy both; both.count1 = both.count2 = 2; oldCurve(both.points1); oldCurve(both.points2);
  EEPROM.put(0, both); EEPROM.writes = 0;
  PumpCalibration identical; check(identical.begin() && identical.heightPointCount() == 2 && EEPROM.writes == 0, "identical legacy tables merge");
  both.points2[1].steps = -20; EEPROM.put(0, both); EEPROM.writes = 0;
  PumpCalibration conflict; check(conflict.begin() && conflict.heightMigrationConflict() && conflict.heightPointCount() == 0 && EEPROM.writes == 0,
                                "conflicting old curves are never silently selected");
  puts("One common height table, independent rates, EEPROM v1/v2 migration and invalid sample checks passed.");
}
