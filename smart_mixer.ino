#include "encoder.hpp"
#include "stepper.hpp"

Encoder encoder;
StepperMotor motor;

void setup(void) {
  Serial.begin(115200);
  encoder.begin();
  motor.begin();
}

void loop(void) {
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
