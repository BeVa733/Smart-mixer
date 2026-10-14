#include "encoder.hpp"
#include "limit_switch.hpp"
#include "pump.hpp"
#include "stepper.hpp"

Encoder encoder;
LimitSwitch upperLimit;
Pump pump;
StepperMotor motor;

static constexpr unsigned long HOMING_TIMEOUT_MS = 30000;
bool homingReady = false;

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

void setup(void) {
  Serial.begin(115200);
  pinMode(LED_BUILTIN, OUTPUT);

  encoder.begin();
  upperLimit.begin();
  pump.begin();
  motor.begin();

  Serial.println(F("Начинаю homing вверх"));

  if (homeUp()) {
    Serial.println(F("Homing завершён: верхний концевик активен"));
  } else {
    Serial.println(F("Ошибка homing: концевик не сработал за 30 секунд"));
  }
}

void loop(void) {
  if (!homingReady) {
    pump.stop();
    return;
  }

  // Для теста насос работает только пока удерживается кнопка энкодера.
  pump.setEnabled(encoder.isButtonPressed());

  digitalWrite(LED_BUILTIN, upperLimit.isTriggered() ? HIGH : LOW);

  const int8_t direction = encoder.update();

  if (direction != 0) {
    if (direction > 0 && upperLimit.isTriggered()) {
      Serial.println(F("Верхний предел: движение вверх запрещено"));
      return;
    }

    if (direction > 0) {
      Serial.println(F("Поворот вправо, шаг двигателя"));
    } else {
      Serial.println(F("Поворот влево, шаг двигателя"));
    }

    motor.step(direction);
  }
}
