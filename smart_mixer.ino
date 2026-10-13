#include "encoder.hpp"
#include "pump.hpp"
#include "stepper.hpp"

Encoder encoder;
Pump pump;
StepperMotor motor;

void setup(void) {
  Serial.begin(115200);
  encoder.begin();
  pump.begin();
  motor.begin();
}

void loop(void) {
  // Для теста насос работает только пока удерживается кнопка энкодера.
  pump.setEnabled(encoder.isButtonPressed());

  const int8_t direction = encoder.update();

  if (direction != 0) {
    if (direction > 0) {
      Serial.println(F("Поворот вправо, шаг двигателя"));
    } else {
      Serial.println(F("Поворот влево, шаг двигателя"));
    }

    motor.step(direction);
  }
}
