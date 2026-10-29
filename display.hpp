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
    line(63, "PRESS = ENTER");
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
