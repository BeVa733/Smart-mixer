// LVGL SDL keyboard adapter with complete UTF-8 characters, not individual bytes.
// Kept on the desktop side; shared widgets use ordinary LVGL input events.
#include <lvgl.h>
#include <src/misc/lv_text_private.h>
#include <SDL2/SDL.h>

namespace {
struct Keys { uint32_t key = 0; bool pressed = false; } keys;
lv_indev_t *keyboard = nullptr;
void readKey(lv_indev_t *, lv_indev_data_t *data) {
  data->key = keys.key;
  data->state = keys.pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}
void emit(uint32_t value) {
  if(!keyboard || !value) return;
  keys.key = value; keys.pressed = true; lv_indev_read(keyboard);
  keys.pressed = false; lv_indev_read(keyboard);
}
}
extern "C" lv_indev_t *lv_sdl_keyboard_create(void) {
  keyboard = lv_indev_create(); lv_indev_set_type(keyboard, LV_INDEV_TYPE_KEYPAD);
  lv_indev_set_read_cb(keyboard, readKey); lv_indev_set_mode(keyboard, LV_INDEV_MODE_EVENT);
  lv_indev_add_event_cb(keyboard, [](lv_event_t *) { keyboard = nullptr; }, LV_EVENT_DELETE, nullptr);
  return keyboard;
}
extern "C" void lv_sdl_keyboard_handler(SDL_Event *event) {
  if(event->type == SDL_TEXTINPUT) {
    uint32_t offset = 0;
    while(event->text.text[offset]) {
      uint32_t codepoint = lv_text_encoded_next(event->text.text, &offset);
      if(!codepoint) break;
      emit(lv_text_unicode_to_encoded(codepoint));
    }
  } else if(event->type == SDL_KEYDOWN) {
    switch(event->key.keysym.sym) {
    case SDLK_RIGHT: emit(LV_KEY_RIGHT); break;
    case SDLK_LEFT: emit(LV_KEY_LEFT); break;
    case SDLK_UP: emit(LV_KEY_UP); break;
    case SDLK_DOWN: emit(LV_KEY_DOWN); break;
    case SDLK_ESCAPE: emit(LV_KEY_ESC); break;
    case SDLK_BACKSPACE: emit(LV_KEY_BACKSPACE); break;
    case SDLK_DELETE: emit(LV_KEY_DEL); break;
    case SDLK_HOME: emit(LV_KEY_HOME); break;
    case SDLK_END: emit(LV_KEY_END); break;
    case SDLK_KP_ENTER: case SDLK_RETURN: emit(LV_KEY_ENTER); break;
    case SDLK_TAB: emit((event->key.keysym.mod & KMOD_SHIFT) ? LV_KEY_PREV : LV_KEY_NEXT); break;
    default: break; // Printable symbols arrive exactly once through TEXTINPUT.
    }
  }
}
