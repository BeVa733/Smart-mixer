#pragma once

#include <Arduino.h>

class Encoder {
private:
  static constexpr uint8_t DT = 3;
  static constexpr uint8_t CLK = 2;

  uint8_t previousState = 0;
  int8_t stepAccumulator = 0;

public:
  void begin() {
    pinMode(CLK, INPUT_PULLUP);
    pinMode(DT, INPUT_PULLUP);

    previousState = readState();
  }

  int8_t update() {
    const uint8_t currentState = readState();

    static constexpr int8_t transitions[16] = {0,  -1, 1, 0, 1, 0, 0,  -1,
                                               -1, 0,  0, 1, 0, 1, -1, 0};

    const uint8_t transition = (previousState << 2) | currentState;
    stepAccumulator += transitions[transition];
    previousState = currentState;

    if (stepAccumulator >= 4) {
      stepAccumulator = 0;
      return 1;
    } else if (stepAccumulator <= -4) {
      stepAccumulator = 0;
      return -1;
    }

    return 0;
  }

private:
  uint8_t readState() const {
    return (static_cast<uint8_t>(digitalRead(CLK)) << 1) |
           static_cast<uint8_t>(digitalRead(DT));
  }
};
