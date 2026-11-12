#include <Arduino.h>

#include "calibration.hpp"
#include "encoder.hpp"
#include "limit_switch.hpp"
#include "pump.hpp"
#include "stepper.hpp"

static constexpr unsigned long HOMING_TIMEOUT_MS = 30000;
static constexpr unsigned long CALIBRATION_RUN_MS = 20000;
static constexpr float CALIBRATION_CHECK_VOLUME_ML = 50.0f;
static constexpr uint16_t HEIGHT_CALIBRATION_INCREMENT_ML = 5;
static constexpr int32_t MAX_DOWNWARD_MICROSTEPS = 650;

PumpCalibration calibration;
Encoder encoder;
LimitSwitch upperLimit;
Pump pump1(8);
Pump pump2(10);
StepperMotor motor;

enum class ControlMode { Idle, Pump1, Pump2, Actuator, Calibration };
ControlMode mode = ControlMode::Idle;

bool homingReady = false;
bool pump1Enabled = false;
bool pump2Enabled = false;
bool calibrationRunning = false;
unsigned long calibrationStartedAt = 0;
unsigned long calibrationDurationMs = 0;
uint8_t selectedCalibrationItem = 0;
int32_t actuatorPositionSteps = 0;
bool heightCalibrationRunning = false;
bool heightCalibrationAwaitingMark = false;
uint8_t heightCalibrationPumpNumber = 0;
uint16_t heightCalibrationVolumeMl = HEIGHT_CALIBRATION_INCREMENT_ML;

const char *calibrationItemName(uint8_t item) {
  static const char *const items[] = {"P1 PRIME", "P2 PRIME", "P1 20 SEC",
                                      "P2 20 SEC", "P1 50 ML", "P2 50 ML"};
  return items[item];
}

uint8_t calibrationPumpNumber() { return selectedCalibrationItem % 2 + 1; }

Pump &calibrationPump() {
  return calibrationPumpNumber() == 1 ? pump1 : pump2;
}

Pump &heightCalibrationPump() {
  return heightCalibrationPumpNumber == 1 ? pump1 : pump2;
}

void printHeightCalibrationTable() {
  const uint8_t count = calibration.heightPointCount();
  const PumpCalibration::HeightPoint *points =
      calibration.heightPoints();

  Serial.println(F("Общая таблица уровня:"));
  if (count == 0) {
    Serial.println(F("  пока нет сохранённых точек"));
    return;
  }

  for (uint8_t i = 0; i < count; ++i) {
    Serial.print(F("  "));
    Serial.print(points[i].volumeMl);
    Serial.print(F(" мл -> "));
    Serial.print(points[i].steps);
    Serial.println(F(" микрошагов от верхнего концевика"));
  }
}

void printHelp() {
  Serial.println(F("\nКоманды Serial Monitor, скорость 115200:"));
  Serial.println(F("  help              эта справка"));
  Serial.println(F("  menu              показать меню"));
  Serial.println(F("  status            состояние системы"));
  Serial.println(F("  home              повторить поиск верхнего концевика"));
  Serial.println(F("  p1 on / p1 off    насос 1 постоянно вкл/выкл"));
  Serial.println(F("  p2 on / p2 off    насос 2 постоянно вкл/выкл"));
  Serial.println(F("  d1 50 / d2 50     дозировать 50 мл"));
  Serial.println(F("  t1 12.5 / t2 8    насос работает заданное время, с"));
  Serial.println(F("  act               включить режим движения энкодером"));
  Serial.println(F("  a+ / a-           один шаг привода вверх/вниз"));
  Serial.println(F("  a 100 / a -100    выполнить указанное число шагов"));
  Serial.println(F("  cal               список калибровочных команд"));
  Serial.println(F("  cal prime 1       прокачка насоса 1"));
  Serial.println(F("  cal prime 2       прокачка насоса 2"));
  Serial.println(F("  cal 20 1          замер насоса 1 за 20 секунд"));
  Serial.println(F("  cal 20 2          замер насоса 2 за 20 секунд"));
  Serial.println(F("  cal 50 1          проверка дозирования 50 мл"));
  Serial.println(F("  cal 50 2          проверка дозирования 50 мл"));
  Serial.println(F("  c1 27.3 / c2 23.9 сохранить граммы после замера 20 секунд"));
  Serial.println(F("  hcal start 1/2  общая калибровка уровня; насос для налива"));
  Serial.println(F("  hcal mark       сохранить текущий уровень"));
  Serial.println(F("  hcal show       показать общую таблицу уровня"));
  Serial.println(F("  hcal clear      стереть общую таблицу уровня"));
  Serial.println(F("  hcal stop       остановить калибровку высоты"));
  Serial.println(F("  s                 аварийно остановить насосы и калибровку"));
}

void printMenu() {
  Serial.println(F("\n=== SMART MIXER / SERIAL ==="));
  Serial.println(F("p1 on/off   насос 1"));
  Serial.println(F("p2 on/off   насос 2"));
  Serial.println(F("d1 50       дозировать насосом 1"));
  Serial.println(F("act         двигать привод энкодером"));
  Serial.println(F("a+ / a-     двигать привод из Serial"));
  Serial.println(F("cal         калибровка"));
  Serial.println(F("hcal start 1  калибровка высоты по 5 мл"));
  Serial.println(F("s           остановить всё"));
  Serial.println(F("help        полный список команд"));
}

void printStatus() {
#ifdef SMART_MIXER_MANUAL_HOME
  Serial.println(F("MIX_API 3"));
#endif
  Serial.println(F("\n--- Состояние ---"));
  Serial.print(F("Homing: "));
  Serial.println(homingReady ? F("готов") : F("не выполнен"));
  Serial.print(F("Верхний концевик: "));
  Serial.println(upperLimit.isTriggered() ? F("активен") : F("не активен"));
  Serial.print(F("P1: "));
  Serial.print(pump1.flowRate(), 3);
  Serial.print(F(" мл/с, "));
  Serial.println(pump1Enabled ? F("включён") : F("выключен"));
  Serial.print(F("P2: "));
  Serial.print(pump2.flowRate(), 3);
  Serial.print(F(" мл/с, "));
  Serial.println(pump2Enabled ? F("включён") : F("выключен"));
  Serial.print(F("Калибровка: "));
  if (calibrationRunning) {
    Serial.println(calibrationItemName(selectedCalibrationItem));
  } else {
    Serial.println(F("не выполняется"));
  }
  Serial.print(F("Положение привода: "));
  Serial.print(actuatorPositionSteps);
  Serial.println(F(" микрошагов от верхнего концевика"));
  Serial.print(F("Калибровка высоты: "));
  if (heightCalibrationRunning) {
    Serial.print(F("общая, насос для налива P"));
    Serial.print(heightCalibrationPumpNumber);
    Serial.print(F(", порция "));
    Serial.print(heightCalibrationVolumeMl);
    Serial.print(F(" мл"));
    Serial.println(heightCalibrationAwaitingMark ? F(" — ждёт hcal mark")
                                                : F(" — насос работает"));
  } else {
    Serial.println(F("не выполняется"));
  }
}

bool homeUp() {
  const unsigned long startTime = millis();
  digitalWrite(LED_BUILTIN, LOW);

  Serial.println(F("Homing: движение вверх..."));
  while (!upperLimit.isTriggered() &&
         millis() - startTime < HOMING_TIMEOUT_MS) {
    motor.stepSlow(1);
  }

  homingReady = upperLimit.isTriggered();
  if (homingReady) {
    actuatorPositionSteps = 0;
  }
  digitalWrite(LED_BUILTIN, homingReady ? HIGH : LOW);
  Serial.println(homingReady ? F("Homing завершён")
                             : F("Ошибка: верхний концевик не найден"));
  return homingReady;
}

void stopPumps() {
  pump1Enabled = false;
  pump2Enabled = false;
  pump1.stop();
  pump2.stop();
}

void stopHeightCalibration(bool printMessage = true) {
  if (heightCalibrationRunning) {
    heightCalibrationPump().stop();
    heightCalibrationRunning = false;
    heightCalibrationAwaitingMark = false;
    if (printMessage) {
      Serial.println(F("Калибровка высоты остановлена"));
    }
  }
}

void stopCalibration(bool completed) {
  if (!calibrationRunning) {
    return;
  }

  calibrationPump().stop();
  calibrationRunning = false;

  if (completed && selectedCalibrationItem >= 2 &&
      selectedCalibrationItem <= 3) {
    const uint8_t pumpNumber = calibrationPumpNumber();
    Serial.print(F("Замер завершён. Взвесь воду и введи: c"));
    Serial.print(pumpNumber);
    Serial.println(F(" <граммы>, например c1 27.3"));
  } else {
    Serial.println(F("Калибровка остановлена"));
  }
}

void startCalibration() {
  if (!homingReady) {
    Serial.println(F("Сначала выполните home и убедитесь, что концевик найден"));
    return;
  }

  stopCalibration(false);
  stopHeightCalibration(false);
  stopPumps();

  Pump &pump = calibrationPump();
  if (selectedCalibrationItem >= 4 && pump.flowRate() <= 0) {
    Serial.println(F("Сначала выполните замер 20 секунд и введите c1/c2"));
    return;
  }

  mode = ControlMode::Calibration;
  calibrationStartedAt = millis();
  calibrationRunning = true;
  calibrationDurationMs = 0;

  if (selectedCalibrationItem == 2 || selectedCalibrationItem == 3) {
    calibrationDurationMs = CALIBRATION_RUN_MS;
  } else if (selectedCalibrationItem >= 4) {
    calibrationDurationMs = pump.msForVolume(CALIBRATION_CHECK_VOLUME_ML);
  }

  pump.setEnabled(true);
  Serial.print(F("Запущено: "));
  Serial.println(calibrationItemName(selectedCalibrationItem));
}

void updateCalibration() {
  if (!calibrationRunning) {
    return;
  }

  const unsigned long elapsed = millis() - calibrationStartedAt;
  if (calibrationDurationMs > 0 && elapsed >= calibrationDurationMs) {
    stopCalibration(true);
    return;
  }

  static unsigned long lastReportAt = 0;
  if (lastReportAt == 0 || millis() - lastReportAt >= 1000) {
    Serial.print(F("Калибровка "));
    Serial.print(calibrationItemName(selectedCalibrationItem));
    Serial.print(F(": "));
    Serial.print(elapsed / 1000);
    Serial.println(F(" с"));
    lastReportAt = millis();
  }
}

void setContinuousPump(uint8_t pumpNumber, bool enabled) {
  if (!homingReady) {
    Serial.println(F("Нельзя включить насос до успешного home"));
    return;
  }

  stopCalibration(false);
  stopHeightCalibration(false);
  stopPumps();

  if (pumpNumber == 1) {
    pump1Enabled = enabled;
    pump1.setEnabled(enabled);
    mode = ControlMode::Pump1;
  } else {
    pump2Enabled = enabled;
    pump2.setEnabled(enabled);
    mode = ControlMode::Pump2;
  }

  Serial.print(F("Насос "));
  Serial.print(pumpNumber);
  Serial.println(enabled ? F(" включён") : F(" выключен"));
}

void dosePump(uint8_t pumpNumber, float volumeMl) {
  if (!homingReady) {
    Serial.println(F("Нельзя дозировать до успешного home"));
    return;
  }

  Pump *pump = pumpNumber == 1 ? &pump1 : (pumpNumber == 2 ? &pump2 : nullptr);
  if (pump == nullptr || volumeMl <= 0) {
    Serial.println(F("Пример: d1 50"));
    return;
  }
  if (pump->flowRate() <= 0) {
    Serial.println(F("Сначала откалибруйте насос командой cal 20 1/2 и c1/c2"));
    return;
  }

  stopCalibration(false);
  stopHeightCalibration(false);
  stopPumps();
  mode = pumpNumber == 1 ? ControlMode::Pump1 : ControlMode::Pump2;
  pump->startDose(volumeMl);

  Serial.print(F("Насос "));
  Serial.print(pumpNumber);
  Serial.print(F(": дозирование "));
  Serial.print(volumeMl, 1);
  Serial.print(F(" мл, "));
  Serial.print(pump->msForVolume(volumeMl));
  Serial.println(F(" мс"));
}

// Timed run for line priming and washing: t1 12.5 = pump 1 for 12.5 s.
// Timed by Arduino, so a lost USB link cannot leave the pump running.
void runPumpFor(uint8_t pumpNumber, float seconds) {
  if (!homingReady) {
    Serial.println(F("Нельзя включить насос до успешного home"));
    return;
  }
  if ((pumpNumber != 1 && pumpNumber != 2) || !(seconds >= 0.1f) || seconds > 600.0f) {
    Serial.println(F("Пример: t1 12.5 (0.1-600 с)"));
    return;
  }

  stopCalibration(false);
  stopHeightCalibration(false);
  stopPumps();
  Pump &pump = pumpNumber == 1 ? pump1 : pump2;
  mode = pumpNumber == 1 ? ControlMode::Pump1 : ControlMode::Pump2;
  pump.startFor(static_cast<unsigned long>(seconds * 1000.0f + 0.5f));

  Serial.print(F("Насос "));
  Serial.print(pumpNumber);
  Serial.print(F(": работа "));
  Serial.print(seconds, 1);
  Serial.println(F(" с"));
}

void moveActuator(long steps) {
  if (!homingReady) {
    Serial.println(F("Нельзя двигать привод до успешного home"));
    return;
  }
  if (steps == 0) {
    return;
  }

  if (!heightCalibrationRunning) {
    stopCalibration(false);
    stopPumps();
  }
  mode = ControlMode::Actuator;

  const int8_t direction = steps > 0 ? 1 : -1;
  const unsigned long count = steps > 0 ? steps : -steps;
  unsigned long moved = 0;

  for (unsigned long i = 0; i < count; ++i) {
    if (direction > 0 && upperLimit.isTriggered()) {
      Serial.println(F("Верхний предел: движение вверх остановлено"));
      break;
    }
    if (direction < 0 &&
        actuatorPositionSteps <= -MAX_DOWNWARD_MICROSTEPS) {
      Serial.println(F("Нижний предел: 650 микрошагов от home"));
      break;
    }
    motor.step(direction);
    actuatorPositionSteps += direction;
    ++moved;
  }

  Serial.print(F("Привод: выполнено микрошагов "));
  Serial.println(direction > 0 ? static_cast<long>(moved)
                               : -static_cast<long>(moved));
}

void startHeightDose() {
  Pump &pump = heightCalibrationPump();
  pump.startDose(static_cast<float>(HEIGHT_CALIBRATION_INCREMENT_ML));

  if (!pump.isDosing()) {
    heightCalibrationRunning = false;
    Serial.println(F("Не удалось запустить порцию: проверьте калибровку насоса"));
    return;
  }

  heightCalibrationAwaitingMark = false;
  Serial.print(F("Насос P"));
  Serial.print(heightCalibrationPumpNumber);
  Serial.print(F(": наливаю "));
  Serial.print(HEIGHT_CALIBRATION_INCREMENT_ML);
  Serial.println(F(" мл. После остановки насоса выставьте уровень энкодером и введите hcal mark"));
}

void updateHeightCalibration() {
  if (!heightCalibrationRunning || heightCalibrationAwaitingMark) {
    return;
  }

  if (!heightCalibrationPump().isDosing()) {
    heightCalibrationAwaitingMark = true;
    Serial.print(F("Порция "));
    Serial.print(heightCalibrationVolumeMl);
    Serial.println(F(" мл готова. Выставьте шток энкодером и введите hcal mark"));
  }
}

void markHeightCalibrationPoint() {
  if (!heightCalibrationRunning) {
    Serial.println(F("Сначала запустите hcal start 1 или hcal start 2"));
    return;
  }
  if (!heightCalibrationAwaitingMark) {
    Serial.println(F("Сначала дождитесь окончания текущей порции"));
    return;
  }

  if (!calibration.addHeightPoint(heightCalibrationVolumeMl,
                                  actuatorPositionSteps)) {
    Serial.println(F("Точка не сохранена: проверьте объём, положение и порядок уровней"));
    return;
  }

  Serial.print(F("Сохранено в общей таблице: "));
  Serial.print(heightCalibrationVolumeMl);
  Serial.print(F(" мл -> "));
  Serial.print(actuatorPositionSteps);
  Serial.println(F(" микрошагов"));

  if (calibration.heightPointCount() >=
      PumpCalibration::MAX_HEIGHT_POINTS) {
    Serial.println(F("Достигнут максимум точек калибровки"));
    stopHeightCalibration(false);
    return;
  }

  heightCalibrationVolumeMl += HEIGHT_CALIBRATION_INCREMENT_ML;
  startHeightDose();
}

void startHeightCalibration(uint8_t pumpNumber) {
  if (pumpNumber != 1 && pumpNumber != 2) {
    Serial.println(F("Номер насоса должен быть 1 или 2"));
    return;
  }
  if (!homingReady) {
    Serial.println(F("Сначала выполните home и убедитесь, что концевик найден"));
    return;
  }

  Pump &pump = pumpNumber == 1 ? pump1 : pump2;
  if (pump.flowRate() <= 0) {
    Serial.println(F("Сначала откалибруйте насос командой cal 20 1/2 и c1/c2"));
    return;
  }

  stopCalibration(false);
  stopHeightCalibration(false);
  stopPumps();

  // The upper limit is the zero reference for the common height table.
  if (!homeUp()) {
    return;
  }

  calibration.clearHeight();
  heightCalibrationPumpNumber = pumpNumber;
  heightCalibrationVolumeMl = HEIGHT_CALIBRATION_INCREMENT_ML;
  heightCalibrationRunning = true;
  heightCalibrationAwaitingMark = false;
  mode = ControlMode::Actuator;

  Serial.print(F("Начата общая калибровка высоты; насос для налива P"));
  Serial.println(pumpNumber);
  startHeightDose();
}

void startHeightCalibrationCommand(const String &arguments) {
  String action = arguments;
  action.trim();

  if (action == "mark") {
    markHeightCalibrationPoint();
    return;
  }
  if (action == "stop") {
    stopHeightCalibration();
    return;
  }
  if (action == "show") { printHeightCalibrationTable(); return; }
  if (action == "clear") {
    if (heightCalibrationRunning) {
      Serial.println(F("Сначала остановите текущую калибровку высоты"));
      return;
    }
    calibration.clearHeight();
    Serial.println(F("Общая таблица уровня очищена"));
    return;
  }
  if (action.length() == 0) {
    Serial.println(F("hcal start 1/2 | hcal mark | hcal show | hcal clear | hcal stop"));
    return;
  }

  const int separator = action.indexOf(' ');
  if (separator < 0) {
    Serial.println(F("Пример: hcal start 1"));
    return;
  }

  const String kind = action.substring(0, separator);
  const uint8_t pumpNumber =
      static_cast<uint8_t>(action.substring(separator + 1).toInt());
  if (pumpNumber != 1 && pumpNumber != 2) {
    Serial.println(F("Номер насоса должен быть 1 или 2"));
    return;
  }

  if (kind == "start") {
    startHeightCalibration(pumpNumber);
  } else {
    Serial.println(F("Команды: start, mark, show, clear, stop"));
  }
}

void saveCalibrationFromSerial(uint8_t pumpNumber, float grams) {
  if (pumpNumber != 1 && pumpNumber != 2) {
    Serial.println(F("Номер насоса должен быть 1 или 2"));
    return;
  }
  if (grams <= 0) {
    Serial.println(F("Масса должна быть больше нуля"));
    return;
  }

  const float rate = grams / (CALIBRATION_RUN_MS / 1000.0f);
  calibration.setRate(pumpNumber, rate);
  if (pumpNumber == 1) {
    pump1.setFlowRate(rate);
  } else {
    pump2.setFlowRate(rate);
  }

  Serial.print(F("Насос "));
  Serial.print(pumpNumber);
  Serial.print(F(": сохранено "));
  Serial.print(rate, 3);
  Serial.println(F(" мл/с"));
}

void startCalibrationCommand(const String &arguments) {
  String action = arguments;
  action.trim();

  if (action.length() == 0) {
    Serial.println(F("cal prime 1/2 | cal 20 1/2 | cal 50 1/2 | cal stop"));
    return;
  }
  if (action == "stop") {
    stopCalibration(false);
    return;
  }

  const int separator = action.indexOf(' ');
  if (separator < 0) {
    Serial.println(F("Пример: cal 20 1"));
    return;
  }

  const String kind = action.substring(0, separator);
  const uint8_t pumpNumber =
      static_cast<uint8_t>(action.substring(separator + 1).toInt());
  if (pumpNumber != 1 && pumpNumber != 2) {
    Serial.println(F("Номер насоса должен быть 1 или 2"));
    return;
  }

  if (kind == "prime") {
    selectedCalibrationItem = pumpNumber == 1 ? 0 : 1;
  } else if (kind == "20") {
    selectedCalibrationItem = pumpNumber == 1 ? 2 : 3;
  } else if (kind == "50") {
    selectedCalibrationItem = pumpNumber == 1 ? 4 : 5;
  } else {
    Serial.println(F("Тип: prime, 20 или 50"));
    return;
  }

  startCalibration();
}

#ifdef SMART_MIXER_MANUAL_HOME
#include "desktop_recipes.hpp"
#endif

void handleSerialCommand() {
#ifdef SMART_MIXER_MANUAL_HOME
  // Read bounded complete lines; never wait for the tail of a USB packet.
  static char input[96];
  static unsigned length = 0;
  static bool discard = false;
  static unsigned long beganAt = 0;
  if(length && millis() - beganAt >= 250) { length = 0; discard = true; }
  bool complete = false;
  for(unsigned budget = 0; budget < 64 && Serial.available(); ++budget) {
    char c = static_cast<char>(Serial.read());
    if(c == '\n') {
      if(!discard && length) { input[length] = 0; complete = true; }
      discard = false;
      if(complete) break;
      length = 0; continue;
    }
    if(c == '\r') continue;
    if(discard) continue;
    if(length == 0) beganAt = millis();
    if(c < 32 || c > 126 || length + 1 >= sizeof(input)) { discard = true; length = 0; continue; }
    input[length++] = c;
  }
  if(!complete) return;
  String command(input); length = 0;
#else
  if (!Serial.available()) {
    return;
  }

  String command = Serial.readStringUntil('\n');
#endif
  command.trim();
  command.toLowerCase();

  if (command.length() == 0) {
    return;
  }
#ifdef SMART_MIXER_MANUAL_HOME
  if (handleDesktopRecipeCommand(command)) return;
#endif
  if (command == "help") {
    printHelp();
    return;
  }
  if (command == "menu") {
    printMenu();
    return;
  }
  if (command == "status") {
    printStatus();
    return;
  }
  if (command == "s" || command == "stop") {
    stopCalibration(false);
    stopHeightCalibration(false);
    stopPumps();
    mode = ControlMode::Idle;
    Serial.println(F("Остановлено"));
    return;
  }
  if (command == "home") {
    stopCalibration(false);
    stopHeightCalibration(false);
    stopPumps();
    homeUp();
    return;
  }
  if (command.startsWith("hcal")) {
    String arguments = command.substring(4);
    startHeightCalibrationCommand(arguments);
    return;
  }
  if (command.startsWith("cal")) {
    String arguments = command.substring(3);
    startCalibrationCommand(arguments);
    return;
  }
  if (command.startsWith("p1 ") || command.startsWith("p2 ")) {
    const uint8_t pumpNumber = command[1] - '0';
    const String action = command.substring(3);
    if (action == "on") {
      setContinuousPump(pumpNumber, true);
    } else if (action == "off") {
      setContinuousPump(pumpNumber, false);
    } else {
      Serial.println(F("Используй p1 on/off или p2 on/off"));
    }
    return;
  }
  if (command.startsWith("d1") || command.startsWith("d2")) {
    const uint8_t pumpNumber = command[1] - '0';
    const float volumeMl = command.substring(2).toFloat();
    dosePump(pumpNumber, volumeMl);
    return;
  }
  if (command.startsWith("t1 ") || command.startsWith("t2 ")) {
    runPumpFor(command[1] - '0', command.substring(3).toFloat());
    return;
  }
  if (command.startsWith("c1") || command.startsWith("c2")) {
    const uint8_t pumpNumber = command[1] - '0';
    const float grams = command.substring(2).toFloat();
    saveCalibrationFromSerial(pumpNumber, grams);
    return;
  }
  if (command == "act") {
    if (!homingReady) {
      Serial.println(F("Нельзя включить привод до успешного home"));
      return;
    }
    if (!heightCalibrationRunning) {
      stopCalibration(false);
      stopPumps();
    }
    mode = ControlMode::Actuator;
    Serial.println(F("Режим привода включён: вращай энкодер"));
    return;
  }
  if (command == "a+") {
    moveActuator(1);
    return;
  }
  if (command == "a-") {
    moveActuator(-1);
    return;
  }
  if (command.startsWith("a ") || command.startsWith("act ")) {
    const uint8_t offset = command.startsWith("a ") ? 1 : 4;
    moveActuator(command.substring(offset).toInt());
    return;
  }

  Serial.println(F("Неизвестная команда. Введи help"));
}

void setup(void) {
  Serial.begin(115200);
  Serial.setTimeout(20);
  pinMode(LED_BUILTIN, OUTPUT);

  const bool calibrationLoaded = calibration.begin();
  pump1.setFlowRate(calibration.pump1MlPerSecond);
  pump2.setFlowRate(calibration.pump2MlPerSecond);

  upperLimit.begin();
  encoder.begin();
  pump1.begin();
  pump2.begin();
  motor.begin();

  Serial.println(F("\nSMART MIXER: режим управления через Serial Monitor"));
  Serial.println(F("Скорость: 115200, окончание строки: Newline"));
  Serial.print(F("Калибровка P1: "));
  Serial.print(pump1.flowRate(), 3);
  Serial.println(F(" мл/с"));
  Serial.print(F("Калибровка P2: "));
  Serial.print(pump2.flowRate(), 3);
  Serial.println(F(" мл/с"));
  if (!calibrationLoaded) {
    Serial.println(F("Калибровка не найдена в EEPROM"));
  }
  if (calibration.heightMigrationConflict()) {
    Serial.println(F("Старые таблицы уровня различаются. Выполните общую калибровку; EEPROM при старте не изменена"));
  }

#ifdef SMART_MIXER_MANUAL_HOME
  Serial.println(F("Для начала работы выполните home из интерфейса ПК"));
#else
  homeUp();
#endif
  printMenu();
}

void loop(void) {
  handleSerialCommand();
#ifdef SMART_MIXER_MANUAL_HOME
  updateRecipe();
#endif

  pump1.update();
  pump2.update();
  updateCalibration();
  updateHeightCalibration();
  digitalWrite(LED_BUILTIN, upperLimit.isTriggered() ? HIGH : LOW);

  if (!homingReady) {
    stopPumps();
    return;
  }

  const int8_t direction = encoder.update();
  if (mode == ControlMode::Actuator && !calibrationRunning && direction != 0) {
    if (direction > 0 && upperLimit.isTriggered()) {
      Serial.println(F("Верхний предел: движение вверх запрещено"));
    } else if (direction < 0 &&
               actuatorPositionSteps <= -MAX_DOWNWARD_MICROSTEPS) {
      Serial.println(F("Нижний предел: 650 микрошагов от home"));
    } else {
      motor.step(direction);
      actuatorPositionSteps += direction;
    }
  }
}
