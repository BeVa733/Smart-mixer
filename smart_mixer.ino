#include "button.hpp"
#include "calibration.hpp"
#include "display.hpp"
#include "encoder.hpp"
#include "limit_switch.hpp"
#include "pump.hpp"
#include "stepper.hpp"

static constexpr float DEFAULT_PUMP1_ML_PER_SEC = 1.364f;
static constexpr float DEFAULT_PUMP2_ML_PER_SEC = 1.197f;
static constexpr unsigned long HOMING_TIMEOUT_MS = 30000;
static constexpr unsigned long CALIBRATION_RUN_MS = 20000;
static constexpr float CALIBRATION_CHECK_VOLUME_ML = 50.0f;

Encoder encoder;
Button button(8);
Display display;
LimitSwitch upperLimit;
PumpCalibration calibration;
Pump pump1(7, DEFAULT_PUMP1_ML_PER_SEC);
Pump pump2(10, DEFAULT_PUMP2_ML_PER_SEC);
StepperMotor motor;

enum class ControlMode { Menu, Pump1, Pump2, Actuator, Calibration };

ControlMode mode = ControlMode::Menu;
uint8_t selectedMenuItem = 0;
uint8_t selectedCalibrationItem = 0;
bool homingReady = false;
bool pump1Enabled = false;
bool pump2Enabled = false;
bool calibrationRunning = false;
unsigned long calibrationStartedAt = 0;
unsigned long calibrationDurationMs = 0;

const char *calibrationItemName(uint8_t item) {
  static const char *const items[] = {"P1 PRIME", "P2 PRIME", "P1 20 SEC",
                                      "P2 20 SEC", "P1 50 ML", "P2 50 ML"};
  return items[item];
}

uint8_t calibrationPumpNumber() { return selectedCalibrationItem % 2 + 1; }

Pump &calibrationPump() {
  return calibrationPumpNumber() == 1 ? pump1 : pump2;
}

bool homeUp() {
  const unsigned long startTime = millis();
  digitalWrite(LED_BUILTIN, LOW);

  while (!upperLimit.isTriggered() &&
         millis() - startTime < HOMING_TIMEOUT_MS) {
    motor.stepSlow(1);
  }

  homingReady = upperLimit.isTriggered();
  digitalWrite(LED_BUILTIN, homingReady ? HIGH : LOW);
  return homingReady;
}

void stopPumps() {
  pump1Enabled = false;
  pump2Enabled = false;
  pump1.stop();
  pump2.stop();
}

void printMenu() {
  Serial.println(F("\nМеню управления:"));
  Serial.print(F("> "));

  switch (selectedMenuItem) {
  case 0:
    Serial.println(F("Насос 1"));
    break;
  case 1:
    Serial.println(F("Насос 2"));
    break;
  case 2:
    Serial.println(F("Привод"));
    break;
  default:
    Serial.println(F("Калибровка"));
    break;
  }

  display.showMenu(selectedMenuItem);
}

void selectNextMenuItem(int8_t direction) {
  if (direction > 0) {
    selectedMenuItem = (selectedMenuItem + 1) % 4;
  } else {
    selectedMenuItem = (selectedMenuItem + 3) % 4;
  }

  printMenu();
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
    display.showCalibrationInput(pumpNumber);
  } else {
    display.showCalibrationMenu(selectedCalibrationItem);
  }
}

void startCalibration() {
  stopPumps();

  Pump &pump = calibrationPump();
  calibrationStartedAt = millis();
  calibrationRunning = true;
  calibrationDurationMs = 0;

  if (selectedCalibrationItem == 2 || selectedCalibrationItem == 3) {
    calibrationDurationMs = CALIBRATION_RUN_MS;
  } else if (selectedCalibrationItem >= 4) {
    calibrationDurationMs = pump.msForVolume(CALIBRATION_CHECK_VOLUME_ML);
  }

  pump.setEnabled(true);
  display.showCalibrationRunning(calibrationItemName(selectedCalibrationItem),
                                 0, calibrationDurationMs);
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

  static unsigned long lastDrawAt = 0;
  if (millis() - lastDrawAt >= 500) {
    display.showCalibrationRunning(calibrationItemName(selectedCalibrationItem),
                                   elapsed, calibrationDurationMs);
    lastDrawAt = millis();
  }
}

// Serial commands:
//   d1 50 / d2 50 — налить заданный объём по текущей калибровке
//   c1 27.3 / c2 23.9 — записать результат 20-секундного замера в граммах
//   s — остановить насосы
void handleSerialCommand() {
  if (!Serial.available()) {
    return;
  }

  String command = Serial.readStringUntil('\n');
  command.trim();

  if (command == "s" || command == "S") {
    stopCalibration(false);
    stopPumps();
    Serial.println(F("Насосы остановлены"));
    return;
  }

  if (command.length() < 4 ||
      (command[0] != 'd' && command[0] != 'D' && command[0] != 'c' &&
       command[0] != 'C')) {
    Serial.println(F("Команды: d1 50, c1 27.3, s"));
    return;
  }

  const int pumpNumber = command.substring(1, 2).toInt();
  const float value = command.substring(3).toFloat();
  Pump *pump = pumpNumber == 1 ? &pump1 : (pumpNumber == 2 ? &pump2 : nullptr);

  if (pump == nullptr || value <= 0) {
    Serial.println(F("Пример: d1 50 или c1 27.3"));
    return;
  }

  if (command[0] == 'c' || command[0] == 'C') {
    const float rate = value / (CALIBRATION_RUN_MS / 1000.0f);
    calibration.setRate(pumpNumber, rate);
    pump->setFlowRate(rate);
    Serial.print(F("Насос "));
    Serial.print(pumpNumber);
    Serial.print(F(": сохранено "));
    Serial.print(rate, 3);
    Serial.println(F(" мл/с"));
    return;
  }

  Serial.print(F("Насос "));
  Serial.print(pumpNumber);
  Serial.print(F(": наливаю "));
  Serial.print(value, 1);
  Serial.print(F(" мл за "));
  Serial.print(pump->msForVolume(value));
  Serial.println(F(" мс"));
  pump->startDose(value);
}

void enterSelectedMode() {
  mode = static_cast<ControlMode>(selectedMenuItem + 1);
  stopPumps();

  switch (mode) {
  case ControlMode::Pump1:
    Serial.println(F("Режим насоса 1. Короткое нажатие: вкл/выкл"));
    display.showPump(1, pump1Enabled);
    break;
  case ControlMode::Pump2:
    Serial.println(F("Режим насоса 2. Короткое нажатие: вкл/выкл"));
    display.showPump(2, pump2Enabled);
    break;
  case ControlMode::Actuator:
    Serial.println(F("Режим привода. Вращение энкодера: движение"));
    display.showActuator(upperLimit.isTriggered());
    break;
  case ControlMode::Calibration:
    Serial.println(F("Калибровка насосов"));
    display.showCalibrationMenu(selectedCalibrationItem);
    break;
  case ControlMode::Menu:
    break;
  }
}

void leaveMode() {
  stopCalibration(false);
  stopPumps();
  mode = ControlMode::Menu;
  printMenu();
}

void handleButtonEvent(ButtonEvent event) {
  switch (event) {
  case ButtonEvent::Click:
    if (mode == ControlMode::Menu) {
      enterSelectedMode();
    } else if (mode == ControlMode::Pump1) {
      pump1Enabled = !pump1Enabled;
      display.showPump(1, pump1Enabled);
    } else if (mode == ControlMode::Pump2) {
      pump2Enabled = !pump2Enabled;
      display.showPump(2, pump2Enabled);
    } else if (mode == ControlMode::Calibration) {
      if (calibrationRunning) {
        stopCalibration(false);
      } else {
        startCalibration();
      }
    }
    break;

  case ButtonEvent::DoubleClick:
    if (mode != ControlMode::Menu) {
      leaveMode();
    }
    break;

  case ButtonEvent::LongPress:
    stopCalibration(false);
    stopPumps();
    mode = ControlMode::Menu;
    Serial.println(F("Аварийная остановка насосов"));
    display.showEmergencyStop();
    printMenu();
    break;

  case ButtonEvent::None:
    break;
  }
}

void setup(void) {
  Serial.begin(115200);
  Serial.setTimeout(20);
  pinMode(LED_BUILTIN, OUTPUT);

  calibration.begin();
  pump1.setFlowRate(calibration.pump1MlPerSecond);
  pump2.setFlowRate(calibration.pump2MlPerSecond);

  display.begin();
  encoder.begin();
  button.begin();
  upperLimit.begin();
  pump1.begin();
  pump2.begin();
  motor.begin();

  Serial.print(F("Калибровка P1: "));
  Serial.print(pump1.flowRate(), 3);
  Serial.println(F(" мл/с"));
  Serial.print(F("Калибровка P2: "));
  Serial.print(pump2.flowRate(), 3);
  Serial.println(F(" мл/с"));

  Serial.println(F("Начинаю homing вверх"));
  display.showHoming();

  if (homeUp()) {
    Serial.println(F("Homing завершён: верхний концевик активен"));
    printMenu();
  } else {
    Serial.println(F("Ошибка homing: концевик не сработал за 30 секунд"));
    display.showHomingError();
  }
}

void loop(void) {
  if (!homingReady) {
    stopPumps();
    return;
  }

  handleButtonEvent(button.update());
  handleSerialCommand();

  pump1.update();
  pump2.update();
  updateCalibration();
  digitalWrite(LED_BUILTIN, upperLimit.isTriggered() ? HIGH : LOW);

  const int8_t direction = encoder.update();

  if (mode == ControlMode::Menu && direction != 0) {
    selectNextMenuItem(direction);
  } else if (mode == ControlMode::Calibration && !calibrationRunning &&
             direction != 0) {
    selectedCalibrationItem =
        (selectedCalibrationItem + 6 + direction) % 6;
    display.showCalibrationMenu(selectedCalibrationItem);
  } else if (mode == ControlMode::Actuator && direction != 0) {
    if (direction > 0 && upperLimit.isTriggered()) {
      Serial.println(F("Верхний предел: движение вверх запрещено"));
      return;
    }

    motor.step(direction);
    display.showActuator(upperLimit.isTriggered());
  }

  if (!calibrationRunning && !pump1.isDosing()) {
    pump1.setEnabled(mode == ControlMode::Pump1 && pump1Enabled);
  }
  if (!calibrationRunning && !pump2.isDosing()) {
    pump2.setEnabled(mode == ControlMode::Pump2 && pump2Enabled);
  }
}
