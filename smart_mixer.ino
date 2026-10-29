#include "button.hpp"
#include "display.hpp"
#include "encoder.hpp"
#include "limit_switch.hpp"
#include "pump.hpp"
#include "stepper.hpp"

Encoder encoder;
Button button(8);
Display display;
LimitSwitch upperLimit;
Pump pump1(7);
Pump pump2(10);
StepperMotor motor;

static constexpr unsigned long HOMING_TIMEOUT_MS = 30000;

enum class ControlMode { Menu, Pump1, Pump2, Actuator };

ControlMode mode = ControlMode::Menu;
uint8_t selectedMenuItem = 0;
bool homingReady = false;
bool pump1Enabled = false;
bool pump2Enabled = false;

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
  default:
    Serial.println(F("Привод"));
    break;
  }

  display.showMenu(selectedMenuItem);
}

void selectNextMenuItem(int8_t direction) {
  if (direction > 0) {
    selectedMenuItem = (selectedMenuItem + 1) % 3;
  } else {
    selectedMenuItem = (selectedMenuItem + 2) % 3;
  }

  printMenu();
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
  case ControlMode::Menu:
    break;
  }
}

void leaveMode() {
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
    }
    break;

  case ButtonEvent::DoubleClick:
    if (mode != ControlMode::Menu) {
      leaveMode();
    }
    break;

  case ButtonEvent::LongPress:
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
  pinMode(LED_BUILTIN, OUTPUT);

  display.begin();
  encoder.begin();
  button.begin();
  upperLimit.begin();
  pump1.begin();
  pump2.begin();
  motor.begin();

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
  digitalWrite(LED_BUILTIN, upperLimit.isTriggered() ? HIGH : LOW);

  const int8_t direction = encoder.update();

  if (mode == ControlMode::Menu && direction != 0) {
    selectNextMenuItem(direction);
  } else if (mode == ControlMode::Actuator && direction != 0) {
    if (direction > 0 && upperLimit.isTriggered()) {
      Serial.println(F("Верхний предел: движение вверх запрещено"));
      return;
    }

    motor.step(direction);
    display.showActuator(upperLimit.isTriggered());
  }

  pump1.setEnabled(mode == ControlMode::Pump1 && pump1Enabled);
  pump2.setEnabled(mode == ControlMode::Pump2 && pump2Enabled);
}
