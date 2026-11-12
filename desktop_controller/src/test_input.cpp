#include <NumericInput.h>
#include <MixerUi.h>
#include <SDL2/SDL.h>
#include <cstring>
#include <cmath>
#include <cstdio>
#include <stdexcept>
#include <vector>
// Feed the same events consumed by lv_sdl_window, without a physical keyboard.
// SDL2-compat/SDL3 cannot safely queue synthetic SDL_TEXTINPUT on this host.
extern "C" void lv_sdl_keyboard_handler(SDL_Event *event);
extern "C" void lv_sdl_mouse_handler(SDL_Event *event);

using namespace smart_mixer;
Uint32 windowId = 0;
void check(bool value, const char *message) { if(!value) throw std::runtime_error(message); }
void pump() { for(int i = 0; i < 10; ++i) { lv_timer_handler(); SDL_Delay(5); } }
void key(SDL_Keycode code) {
  SDL_Event e{}; e.type = SDL_KEYDOWN; e.key.keysym.sym = code;
  e.key.windowID = windowId;
  lv_sdl_keyboard_handler(&e); pump();
  e.type = SDL_KEYUP; lv_sdl_keyboard_handler(&e); pump();
}
void text(const char *value) {
  SDL_Event e{}; e.type = SDL_TEXTINPUT;
  e.text.windowID = windowId;
  SDL_strlcpy(e.text.text, value, sizeof(e.text.text));
  lv_sdl_keyboard_handler(&e); pump();
}
void collect(lv_obj_t *parent, std::vector<lv_obj_t *> &fields) {
  if(lv_obj_check_type(parent, &lv_textarea_class)) fields.push_back(parent);
  for(uint32_t i = 0; i < lv_obj_get_child_count(parent); ++i) collect(lv_obj_get_child(parent, i), fields);
}
lv_obj_t *findButton(lv_obj_t *parent, const char *title) {
  if(lv_obj_has_flag(parent, LV_OBJ_FLAG_CLICKABLE)) {
    auto *child = lv_obj_get_child(parent, 0);
    if(child && lv_obj_check_type(child, &lv_label_class) && strcmp(lv_label_get_text(child), title) == 0) return parent;
  }
  for(uint32_t i = 0; i < lv_obj_get_child_count(parent); ++i)
    if(auto *found = findButton(lv_obj_get_child(parent, i), title)) return found;
  return nullptr;
}
int main() {
  lv_init(); auto *display = lv_sdl_window_create(800, 480);
  windowId = SDL_GetWindowID(SDL_RenderGetWindow(static_cast<SDL_Renderer *>(lv_sdl_window_get_renderer(display))));
  auto *group = lv_group_create(); lv_group_set_default(group);
  lv_sdl_mouse_create(); auto *keyboard = lv_sdl_keyboard_create();
  lv_indev_set_group(keyboard, group);
  NumericInput integer, decimal;
  integer.create(lv_screen_active(), 0, 0, 1, 1000, 50);
  decimal.create(lv_screen_active(), 0, 80, 1, 99999, 2730, 2);
  pump();
  SDL_Event click{}; click.type = SDL_MOUSEBUTTONDOWN;
  click.button.windowID = windowId; click.button.button = SDL_BUTTON_LEFT;
  click.button.x = 30; click.button.y = 100;
  lv_sdl_mouse_handler(&click); pump();
  click.type = SDL_MOUSEBUTTONUP; lv_sdl_mouse_handler(&click); pump();
  check(lv_group_get_focused(group) == decimal.object(), "mouse click must focus text entry for keyboard");
  lv_group_focus_obj(integer.object());
  lv_textarea_set_text(integer.object(), "");
  // A real physical key produces both KEYDOWN and TEXTINPUT: insert only once.
  key(SDLK_5); text("5");
  check(strcmp(lv_textarea_get_text(integer.object()), "5") == 0, "digit duplicated");
  text("a-+ ,.e");
  check(strcmp(lv_textarea_get_text(integer.object()), "5") == 0, "integer accepted non-digit");
  text("0"); float value = 0;
  check(integer.value(value) && value == 50, "text and command value diverged");
  key(SDLK_HOME); key(SDLK_DELETE);
  check(strcmp(lv_textarea_get_text(integer.object()), "0") == 0, "Home/Delete failed");
  key(SDLK_END); key(SDLK_BACKSPACE);
  check(!integer.value(value), "empty input accepted");
  text("1001"); check(!integer.value(value), "out of range accepted");
  key(SDLK_BACKSPACE); check(integer.value(value) && value == 100, "correction failed");
  lv_obj_send_event(findButton(lv_screen_active(), "+"), LV_EVENT_CLICKED, nullptr);
  check(integer.value(value) && value == 101, "plus did not use edited value");
  lv_obj_send_event(findButton(lv_screen_active(), "-"), LV_EVENT_CLICKED, nullptr);
  check(integer.value(value) && value == 100, "minus did not use edited value");
  lv_group_focus_obj(decimal.object()); lv_textarea_set_text(decimal.object(), "");
  text("27,30");
  check(strcmp(lv_textarea_get_text(decimal.object()), "27.30") == 0, "decimal comma normalization");
  text("9.");
  check(strcmp(lv_textarea_get_text(decimal.object()), "27.30") == 0, "extra decimal digits or separator accepted");
  check(decimal.value(value) && std::fabs(value - 27.30f) < 0.001f, "decimal value mismatch");
  lv_textarea_set_text(decimal.object(), "0.00"); check(!decimal.value(value), "zero mass accepted");
  lv_textarea_set_text(decimal.object(), "0.01"); check(decimal.value(value), "minimum mass rejected");
  lv_obj_clean(lv_screen_active());
  MixerUi ui; std::vector<Command> commands;
  ui.create([](void *p, const Command &c) { static_cast<std::vector<Command> *>(p)->push_back(c); }, &commands);
  ui.setConnection(true, true, "test");
  auto *statusButton = findButton(lv_screen_active(), "Состояние");
  auto *stopButton = findButton(lv_screen_active(), "Стоп");
  auto *backButton = findButton(lv_screen_active(), LV_SYMBOL_LEFT);
  check(statusButton && stopButton && backButton, "contextual navigation buttons");
  check(!lv_obj_is_visible(statusButton) && !lv_obj_is_visible(stopButton) && !lv_obj_is_visible(backButton), "main menu has no idle controls");
  ui.showRecipes(); pump();
  check(lv_obj_is_visible(backButton) && !lv_obj_is_visible(statusButton), "recipe navigation without service status");
  ui.showTab(2); pump();
  check(lv_obj_is_visible(statusButton) && !lv_obj_is_visible(stopButton), "idle calibration has status but no stop");
  ui.setCalibrating(true); check(lv_obj_is_visible(stopButton), "stop during calibration");
  ui.showMain(); check(lv_obj_is_visible(stopButton), "active calibration stop remains reachable on main");
  ui.setCalibrating(false); check(!lv_obj_is_visible(stopButton), "stop hidden after calibration finishes");
  ui.setCooking(true, true, "test", 10); check(lv_obj_is_visible(stopButton), "stop during drink preparation");
  ui.setCooking(true, false, "test", 100); check(!lv_obj_is_visible(stopButton), "stop hidden after preparation finishes");
  std::vector<lv_obj_t *> fields; collect(ui.serviceRoot(), fields);
  check(fields.size() == 5, "all fields covered");
  for(unsigned i = 0; i < 4; ++i) {
    ui.showTab(i < 2 ? 0 : i - 1); pump();
    lv_group_focus_obj(fields[i]); lv_textarea_set_text(fields[i], ""); text("42");
    check(strcmp(lv_textarea_get_text(fields[i]), "42") == 0, "keyboard failed on UI field");
  }
  ui.showTab(0); pump();
  auto *dose = findButton(lv_screen_active(), "Налить"); check(dose, "dose button");
  lv_obj_send_event(dose, LV_EVENT_CLICKED, nullptr);
  check(commands.size() == 1 && commands.back().value == 42, "dose does not use typed value");
  lv_textarea_set_text(fields[0], ""); lv_obj_send_event(dose, LV_EVENT_CLICKED, nullptr);
  check(commands.size() == 1, "empty input dispatched a dose");
  auto *notice = findButton(lv_layer_top(), "Понятно"); check(notice, "invalid input is explained outside removed header");
  lv_obj_send_event(notice, LV_EVENT_CLICKED, nullptr);
  ui.showTab(3); pump(); lv_group_focus_obj(fields[4]);
  lv_textarea_set_text(fields[4], ""); text("/dev/ttyACM1");
  check(strcmp(ui.port(), "/dev/ttyACM1") == 0, "USB path keyboard input");
  std::vector<Recipe> saved; std::vector<RecipeAction> recipeActions;
  struct RecipesContext { std::vector<Recipe> *saved; std::vector<RecipeAction> *actions; } context{&saved, &recipeActions};
  ui.setRecipeHandler([](void *p, RecipeAction action, const Recipe &r) {
    auto *ctx = static_cast<RecipesContext *>(p); ctx->saved->push_back(r); ctx->actions->push_back(action);
  }, &context);
  ui.showMain(); pump();
  auto *add = findButton(lv_screen_active(), "Добавить рецепт"); check(add, "main menu add recipe");
  lv_obj_send_event(add, LV_EVENT_CLICKED, nullptr); pump();
  std::vector<lv_obj_t *> recipeFields; collect(ui.recipeEditorRoot(), recipeFields);
  check(recipeFields.size() == 3, "recipe editor fields");
  lv_group_focus_obj(recipeFields[0]); text("Лимонад");
  check(strcmp(lv_textarea_get_text(recipeFields[0]), "Лимонад") == 0, "UTF-8 name must not be split into bytes");
  lv_textarea_set_text(recipeFields[1], "20"); lv_textarea_set_text(recipeFields[2], "30");
  lv_obj_t *order = nullptr;
  for(uint32_t i = 0; i < lv_obj_get_child_count(ui.recipeEditorRoot()); ++i) {
    auto *child = lv_obj_get_child(ui.recipeEditorRoot(), i);
    if(lv_obj_check_type(child, &lv_dropdown_class)) order = child;
  }
  check(order, "first pump selection replaces height table selection");
  lv_dropdown_set_selected(order, 1);
  auto *save = findButton(lv_screen_active(), "Сохранить рецепт"); lv_obj_send_event(save, LV_EVENT_CLICKED, nullptr);
  check(saved.size() == 1 && saved[0].name == "Лимонад" && saved[0].ml1 == 20 && saved[0].ml2 == 30 && saved[0].firstPump == 2, "save recipe with chosen first pump");
  lv_textarea_set_text(recipeFields[1], "0"); lv_textarea_set_text(recipeFields[2], "0");
  lv_obj_send_event(save, LV_EVENT_CLICKED, nullptr); check(saved.size() == 1, "zero-volume recipe rejected");
  saved[0].id = 1; ui.setRecipes(saved); ui.setCooking(true, false, "ready", 0); ui.showRecipes();
  auto *prepare = findButton(lv_screen_active(), "Приготовить"); lv_obj_send_event(prepare, LV_EVENT_CLICKED, nullptr);
  check(recipeActions.size() == 1, "prepare requires confirmation");
  auto *confirm = findButton(lv_layer_top(), "Подтвердить"); check(confirm, "recipe confirmation modal");
  lv_obj_send_event(confirm, LV_EVENT_CLICKED, nullptr);
  check(recipeActions.back() == RecipeAction::Prepare, "confirmed prepare callback");
  puts("SDL keyboard: all fields, no duplicate digits, filtering, decimals, editing, ranges and typed dose passed.");
  lv_sdl_quit(); lv_deinit();
}
