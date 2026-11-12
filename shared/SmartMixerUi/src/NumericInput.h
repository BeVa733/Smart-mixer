#pragma once
#include <lvgl.h>
#include <stdint.h>

namespace smart_mixer {
// Plain textarea: one LVGL keyboard handler, no spinbox ancestor/child double edit.
class NumericInput {
public:
  void create(lv_obj_t *parent, int x, int y, int32_t min, int32_t max,
              int32_t initial, unsigned decimals = 0);
  bool value(float &out) const;
  lv_obj_t *object() const { return field_; }
private:
  lv_obj_t *field_ = nullptr;
  int32_t min_ = 1, max_ = 1, scale_ = 1;
  unsigned decimals_ = 0;
  bool validText(const char *text, bool complete, int32_t &scaled) const;
  void setScaled(int32_t value);
  void adjust(int direction);
  static void inserted(lv_event_t *);
  static void changed(lv_event_t *);
  static void stepped(lv_event_t *);
};
}
