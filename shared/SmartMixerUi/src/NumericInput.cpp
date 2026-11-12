#include "NumericInput.h"
#include <stdio.h>
#include <string.h>

namespace smart_mixer {
void NumericInput::create(lv_obj_t *parent, int x, int y, int32_t min,
                          int32_t max, int32_t initial, unsigned decimals) {
  min_ = min; max_ = max; decimals_ = decimals; scale_ = decimals ? 100 : 1;
  field_ = lv_textarea_create(parent);
  lv_obj_set_pos(field_, x, y); lv_obj_set_size(field_, 115, 48);
  lv_textarea_set_one_line(field_, true);
  lv_textarea_set_accepted_chars(field_, decimals ? "0123456789.," : "0123456789");
  lv_textarea_set_max_length(field_, decimals ? 7 : 4);
  lv_obj_add_event_cb(field_, inserted, LV_EVENT_INSERT, this);
  lv_obj_add_event_cb(field_, changed, LV_EVENT_VALUE_CHANGED, this);
  setScaled(initial);
  for(int direction = -1; direction <= 1; direction += 2) {
    auto *button = lv_button_create(parent);
    lv_obj_set_pos(button, x + (direction < 0 ? 120 : 172), y);
    lv_obj_set_size(button, 48, 48);
    lv_obj_remove_flag(button, LV_OBJ_FLAG_CLICK_FOCUSABLE);
    auto *text = lv_label_create(button);
    lv_label_set_text(text, direction < 0 ? "-" : "+"); lv_obj_center(text);
    lv_obj_set_user_data(button, field_);
    lv_obj_add_event_cb(button, stepped, LV_EVENT_CLICKED, this);
  }
}

bool NumericInput::validText(const char *text, bool complete, int32_t &scaled) const {
  int32_t whole = 0, fraction = 0;
  unsigned digits = 0, fractionDigits = 0;
  bool point = false;
  for(const char *p = text; *p; ++p) {
    if(*p == '.' && decimals_ && !point) { point = true; continue; }
    if(*p < '0' || *p > '9') return false;
    ++digits;
    if(point) {
      if(++fractionDigits > decimals_) return false;
      fraction = fraction * 10 + *p - '0';
    } else {
      if(whole > 1000) return false;
      whole = whole * 10 + *p - '0';
    }
  }
  if(!complete) return true; // empty / 0 / trailing point are editable intermediate states
  if(!digits) return false;
  for(unsigned i = fractionDigits; i < decimals_; ++i) fraction *= 10;
  scaled = whole * scale_ + fraction;
  return scaled >= min_ && scaled <= max_;
}

bool NumericInput::value(float &out) const {
  int32_t scaled;
  if(!validText(lv_textarea_get_text(field_), true, scaled)) return false;
  out = static_cast<float>(scaled) / scale_;
  return true;
}
void NumericInput::setScaled(int32_t v) {
  char text[24];
  if(decimals_) snprintf(text, sizeof(text), "%ld.%02ld", static_cast<long>(v / scale_), static_cast<long>(v % scale_));
  else snprintf(text, sizeof(text), "%ld", static_cast<long>(v));
  lv_textarea_set_text(field_, text);
  lv_textarea_set_cursor_pos(field_, LV_TEXTAREA_CURSOR_LAST);
}
void NumericInput::adjust(int direction) {
  int32_t scaled;
  if(!validText(lv_textarea_get_text(field_), true, scaled)) scaled = min_;
  else scaled += direction;
  if(scaled < min_) scaled = min_;
  if(scaled > max_) scaled = max_;
  setScaled(scaled);
  lv_group_focus_obj(field_);
}
void NumericInput::stepped(lv_event_t *e) {
  auto *self = static_cast<NumericInput *>(lv_event_get_user_data(e));
  auto *button = lv_event_get_target_obj(e);
  const char *text = lv_label_get_text(lv_obj_get_child(button, 0));
  self->adjust(text[0] == '-' ? -1 : 1);
}
void NumericInput::inserted(lv_event_t *e) {
  auto *self = static_cast<NumericInput *>(lv_event_get_user_data(e));
  const char *insert = static_cast<const char *>(lv_event_get_param(e));
  // Textarea sends this marker for Backspace/Delete too. Never block deletion.
  if(insert[0] == LV_KEY_DEL && insert[1] == 0) return;
  if(self->decimals_ && strcmp(insert, ",") == 0) {
    lv_textarea_set_insert_replace(self->field_, ".");
    return;
  }
  const char *text = lv_textarea_get_text(self->field_);
  size_t length = strlen(text), added = strlen(insert);
  size_t pos = lv_textarea_get_cursor_pos(self->field_);
  char candidate[24];
  if(pos > length || length + added >= sizeof(candidate)) {
    lv_textarea_set_insert_replace(self->field_, ""); return;
  }
  memcpy(candidate, text, pos);
  memcpy(candidate + pos, insert, added);
  memcpy(candidate + pos + added, text + pos, length - pos + 1);
  int32_t unused = 0;
  if(!self->validText(candidate, false, unused)) lv_textarea_set_insert_replace(self->field_, "");
}
void NumericInput::changed(lv_event_t *e) {
  auto *self = static_cast<NumericInput *>(lv_event_get_user_data(e));
  float value;
  lv_obj_set_style_border_color(self->field_, lv_color_hex(self->value(value) ? 0xCBD5E1 : 0xBA384B), 0);
}
}
