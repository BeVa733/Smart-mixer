#pragma once

#include <Arduino.h>

enum class ButtonEvent { None, Click, DoubleClick, LongPress };

class Button {
private:
  static constexpr unsigned long DEBOUNCE_MS = 30;
  static constexpr unsigned long DOUBLE_CLICK_MS = 350;
  static constexpr unsigned long LONG_PRESS_MS = 800;

  uint8_t pin;
  bool rawState = false;
  bool stableState = false;
  bool longPressSent = false;
  bool waitingForSecondClick = false;
  unsigned long stateChangedAt = 0;
  unsigned long pressedAt = 0;
  unsigned long firstClickAt = 0;

public:
  explicit Button(uint8_t buttonPin) : pin(buttonPin) {}

  void begin() {
    pinMode(pin, INPUT_PULLUP);
    rawState = isPressed();
    stableState = rawState;
    stateChangedAt = millis();
  }

  ButtonEvent update() {
    const unsigned long now = millis();
    const bool newRawState = isPressed();

    if (newRawState != rawState) {
      rawState = newRawState;
      stateChangedAt = now;
    }

    if (rawState != stableState && now - stateChangedAt >= DEBOUNCE_MS) {
      stableState = rawState;

      if (stableState) {
        pressedAt = now;
        longPressSent = false;
      } else if (!longPressSent) {
        if (waitingForSecondClick && now - firstClickAt <= DOUBLE_CLICK_MS) {
          waitingForSecondClick = false;
          return ButtonEvent::DoubleClick;
        }

        waitingForSecondClick = true;
        firstClickAt = now;
      }
    }

    if (stableState && !longPressSent && now - pressedAt >= LONG_PRESS_MS) {
      longPressSent = true;
      waitingForSecondClick = false;
      return ButtonEvent::LongPress;
    }

    if (waitingForSecondClick && now - firstClickAt > DOUBLE_CLICK_MS) {
      waitingForSecondClick = false;
      return ButtonEvent::Click;
    }

    return ButtonEvent::None;
  }

private:
  bool isPressed() const { return digitalRead(pin) == LOW; }
};
