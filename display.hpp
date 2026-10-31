#pragma once

#include <Arduino.h>
#include <U8g2lib.h>

class Display {
private:
  U8G2_SSD1306_128X64_NONAME_F_HW_I2C oled;

  void clear() {
    oled.clearBuffer();
    oled.setFont(u8g2_font_6x10_tf);
  }

  void line(uint8_t y, const char *text) { oled.drawStr(0, y, text); }

  void finish() { oled.sendBuffer(); }

public:
  Display() : oled(U8G2_R0, U8X8_PIN_NONE) {}

  void begin() {
    oled.setI2CAddress(0x3C << 1);
    oled.begin();
    showTitle("SMART MIXER");
  }

  void showTitle(const char *title) {
    clear();
    line(16, title);
    finish();
  }

  void showHoming() {
    clear();
    line(16, "HOMING");
    line(32, "Moving up...");
    finish();
  }

  void showHomingError() {
    clear();
    line(16, "HOMING ERROR");
    line(32, "Check limit switch");
    finish();
  }

  void showMenu(uint8_t selected) {
    clear();
    line(10, "SELECT NODE");
    oled.drawStr(0, 26, selected == 0 ? "> PUMP 1" : "  PUMP 1");
    oled.drawStr(0, 38, selected == 1 ? "> PUMP 2" : "  PUMP 2");
    oled.drawStr(0, 50, selected == 2 ? "> ACTUATOR" : "  ACTUATOR");
    oled.drawStr(0, 62, selected == 3 ? "> CALIBRATION" : "  CALIBRATION");
    finish();
  }

  void showCalibrationMenu(uint8_t selected) {
    static const char *const items[] = {"P1 PRIME", "P2 PRIME", "P1 20 SEC",
                                        "P2 20 SEC", "P1 50 ML", "P2 50 ML"};
    const uint8_t first = selected < 4 ? 0 : selected - 3;

    clear();
    line(10, "PUMP CALIBRATION");
    for (uint8_t row = 0; row < 4; ++row) {
      const uint8_t item = first + row;
      oled.drawStr(0, 23 + row * 10, item == selected ? ">" : " ");
      oled.drawStr(8, 23 + row * 10, items[item]);
    }
    line(63, "CLICK = START");
    finish();
  }

  void showCalibrationRunning(const char *item, unsigned long elapsedMs,
                              unsigned long durationMs) {
    char value[22];
    clear();
    line(10, item);
    line(25, "RUNNING");

    if (durationMs == 0) {
      snprintf(value, sizeof(value), "%lu s", elapsedMs / 1000);
    } else {
      const unsigned long left =
          elapsedMs >= durationMs ? 0 : durationMs - elapsedMs;
      snprintf(value, sizeof(value), "%lu s left", (left + 999) / 1000);
    }
    oled.setFont(u8g2_font_logisoso16_tf);
    oled.drawStr(0, 50, value);
    oled.setFont(u8g2_font_6x10_tf);
    line(63, "CLICK = STOP");
    finish();
  }

  void showCalibrationInput(uint8_t pumpNumber) {
    char command[22];
    clear();
    line(10, "WEIGH SAMPLE");
    snprintf(command, sizeof(command), "SEND: c%u grams", pumpNumber);
    line(28, command);
    line(43, "Example: c1 27.3");
    line(63, "DOUBLE = MENU");
    finish();
  }

  void showCalibrationMissing() {
    clear();
    line(16, "NO CALIBRATION");
    line(34, "RUN 20 SEC TEST");
    line(52, "THEN SEND c1/c2");
    finish();
  }

  void showCalibrationCheck(uint8_t pumpNumber, float rate) {
    char value[24];
    clear();
    snprintf(value, sizeof(value), "PUMP %u RATE", pumpNumber);
    line(16, value);
    snprintf(value, sizeof(value), "%.3f ml/s", rate);
    line(34, value);
    line(52, "50 ML CHECK");
    line(63, "DOUBLE = MENU");
    finish();
  }

  void showPump(uint8_t number, bool enabled) {
    clear();
    oled.drawStr(0, 16, number == 1 ? "PUMP 1" : "PUMP 2");
    line(34, enabled ? "STATUS: ON" : "STATUS: OFF");
    line(50, "CLICK: TOGGLE");
    line(62, "DOUBLE: MENU");
    finish();
  }

  void showActuator(bool atUpperLimit) {
    clear();
    line(0, "ACTUATOR");
    line(20, "TURN: MOVE");
    line(36, atUpperLimit ? "UPPER LIMIT" : "LIMIT: CLEAR");
    line(52, "DOUBLE: MENU");
    finish();
  }

  void showEmergencyStop() {
    clear();
    line(16, "STOPPED");
    line(34, "LONG PRESS");
    finish();
  }
};
