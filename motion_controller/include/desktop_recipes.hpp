#pragma once
#include <RecipeExecution.h>
#include <math.h>

// Included after the existing service functions; only desktop_usb compiles it.
smart_mixer::RecipeExecution recipeExecution;
unsigned long recipeReportedAt = 0;
int recipePump = 0;

const char *recipePhaseName() {
  using smart_mixer::DrinkPhase;
  switch(recipeExecution.phase()) {
  case DrinkPhase::Positioning: return "POSITIONING";
  case DrinkPhase::Lowering: return "LOWERING";
  case DrinkPhase::FirstLayer: return "FIRST";
  case DrinkPhase::SecondLayer: return "SECOND";
  case DrinkPhase::Returning: return "RETURNING";
  case DrinkPhase::Done: return "DONE";
  case DrinkPhase::Cancelled: return "CANCELLED";
  default: return "ERROR";
  }
}
void reportRecipe(uint32_t id, const char *phase, int percent, const char *reason) {
  char report[100];
  int length = snprintf(report, sizeof(report), "MIX %lu %s %d %s\n",
    static_cast<unsigned long>(id), phase, percent, reason);
  if(length > 0 && length < static_cast<int>(sizeof(report)) && Serial && Serial.availableForWrite() >= length)
    Serial.write(reinterpret_cast<const uint8_t *>(report), length);
}
void reportRecipe() {
  reportRecipe(recipeExecution.id(), recipePhaseName(), recipeExecution.percent(), recipeExecution.error());
  recipeReportedAt = millis();
}
void cancelRecipe() {
  if(!recipeExecution.active()) return;
  recipeExecution.cancel(); stopPumps(); recipePump = 0; reportRecipe();
}
bool handleDesktopRecipeCommand(const String &command) {
  if(command.startsWith("lease ")) {
    uint32_t id, max = UINT32_MAX;
    if(smart_mixer::parseRecipeValues(command.c_str() + 6, &id, &max, 1)) recipeExecution.heartbeat(id, millis());
    return true;
  }
  if(command.startsWith("mix ")) {
    // API 3: mix id ml1 ml2 firstPump layered(1)/mixed(0). API 2 form without the mode is layered.
    uint32_t v[5]{}; const uint32_t limits[] = {UINT32_MAX, 1000, 1000, 2, 1};
    if(!smart_mixer::parseRecipeValues(command.c_str() + 4, v, limits, 5)) {
      for(auto &value : v) value = 0;
      if(!smart_mixer::parseRecipeValues(command.c_str() + 4, v, limits, 4)) return true;
      v[4] = 1;
    }
    if(!v[0] || !v[3]) return true;
    if(recipeExecution.active()) {
      if(recipeExecution.id() == v[0]) reportRecipe();
      else reportRecipe(v[0], "ERROR", 0, "BUSY");
      return true;
    }
    if(!homingReady || calibrationRunning || heightCalibrationRunning || pump1Enabled || pump2Enabled || pump1.isDosing() || pump2.isDosing()) {
      reportRecipe(v[0], "ERROR", 0, "NOT_READY"); return true;
    }
    // Rates come from EEPROM for every drink, not from the copy loaded at boot.
    reloadCalibrationFromEeprom();
    printPumpRates();
    double duration1 = v[1] ? v[1] * 1000.0 / pump1.flowRate() : 0;
    double duration2 = v[2] ? v[2] * 1000.0 / pump2.flowRate() : 0;
    if(!isfinite(duration1) || !isfinite(duration2) || duration1 < 0 || duration2 < 0 ||
       duration1 > 1800000 || duration2 > 1800000) {
      reportRecipe(v[0], "ERROR", 0, "PUMP_CALIBRATION"); return true;
    }
    smart_mixer::HeightSample points[20];
    unsigned count = calibration.heightPointCount();
    const auto *stored = calibration.heightPoints();
    for(unsigned i = 0; i < count && i < 20; ++i) { points[i].ml = stored[i].volumeMl; points[i].steps = stored[i].steps; }
    if(!recipeExecution.start(v[0], v[1], v[2], static_cast<uint32_t>(duration1), static_cast<uint32_t>(duration2), points, count, millis(), v[3], v[4] != 0)) {
      reportRecipe(v[0], "ERROR", 0, "CALIBRATION_OR_DUPLICATE"); return true;
    }
    // Log the planned pump times so a short pour can be checked against the calibration.
    for(int p = 1; p <= 2; ++p) {
      Serial.print(F("Рецепт: P")); Serial.print(p); Serial.print(F(" "));
      Serial.print(v[p]); Serial.print(F(" мл -> "));
      Serial.print(static_cast<unsigned long>(p == 1 ? duration1 : duration2)); Serial.print(F(" мс по "));
      Serial.print((p == 1 ? pump1 : pump2).flowRate(), 3); Serial.println(F(" мл/с"));
    }
    stopPumps(); recipePump = 0; mode = ControlMode::Idle; reportRecipe(); return true;
  }
  if(recipeExecution.active()) {
    if(command == "s" || command == "stop") { cancelRecipe(); return false; }
    if(command == "status") reportRecipe();
    else reportRecipe(recipeExecution.id(), recipePhaseName(), recipeExecution.percent(), "BUSY");
    return true;
  }
  return false;
}
void updateRecipe() {
  if(!recipeExecution.active()) return;
  auto previous = recipeExecution.phase();
  int direction = recipeExecution.step(millis(), actuatorPositionSteps, upperLimit.isTriggered());
  int wantedPump = recipeExecution.pump();
  if(recipePump != wantedPump) {
    stopPumps(); recipePump = wantedPump;
    if(recipePump == 1) pump1.setEnabled(true);
    if(recipePump == 2) pump2.setEnabled(true);
  }
  if(direction) { motor.step(direction); actuatorPositionSteps += direction; }
  if(previous != recipeExecution.phase() || millis() - recipeReportedAt >= 200) reportRecipe();
  // After a finished drink find the upper zero again (not after cancel/error: keep the shaft still).
  // Blocking like the "home" command; DONE is reported before it.
  if(previous != smart_mixer::DrinkPhase::Done && recipeExecution.phase() == smart_mixer::DrinkPhase::Done) {
    Serial.flush();
    homeUp();
  }
}
