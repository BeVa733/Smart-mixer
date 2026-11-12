#include "MixerUi.h"
#include <stdio.h>
#include <initializer_list>
LV_FONT_DECLARE(mixer_font_16);

namespace smart_mixer {
namespace {
lv_obj_t *label(lv_obj_t *parent, const char *text, int x, int y, int width) {
  auto *o = lv_label_create(parent);
  lv_obj_set_pos(o, x, y);
  lv_obj_set_width(o, width);
  lv_label_set_text(o, text);
  return o;
}
void cancelButton(lv_obj_t *box, const char *title = "Отмена") {
  auto *cancel = lv_msgbox_add_footer_button(box, title);
  lv_obj_set_style_text_font(cancel, &mixer_font_16, 0);
  lv_obj_add_event_cb(cancel, [](lv_event_t *e) {
    lv_msgbox_close(static_cast<lv_obj_t *>(lv_event_get_user_data(e)));
  }, LV_EVENT_CLICKED, box);
}
}

lv_obj_t *MixerUi::button(lv_obj_t *p, const char *text, int x, int y,
                         int width, Action action, int pump, bool guarded) {
  auto *b = lv_button_create(p);
  lv_obj_set_style_text_font(b, &mixer_font_16, 0);
  lv_obj_set_pos(b, x, y); lv_obj_set_size(b, width, 48);
  auto *t = lv_label_create(b); lv_label_set_text(t, text); lv_obj_center(t);
  bindings_[count_] = {this, action, pump};
  lv_obj_add_event_cb(b, clicked, LV_EVENT_CLICKED, &bindings_[count_++]);
  if(guarded) controls_[controlCount_++] = b;
  return b;
}

void MixerUi::create(CommandHandler handler, void *context) {
  handler_ = handler; context_ = context;
  auto *screen = lv_screen_active();
  lv_obj_set_style_bg_color(screen, lv_color_hex(0xF7F7F9), 0);
  lv_obj_set_style_text_color(screen, lv_color_hex(0x20252C), 0);
  lv_obj_set_style_text_font(screen, &mixer_font_16, 0);
  tabs_ = lv_tabview_create(screen);
  lv_obj_set_style_text_font(tabs_, &mixer_font_16, 0);
  lv_obj_set_style_text_font(lv_tabview_get_tab_bar(tabs_), &mixer_font_16, 0);
  lv_obj_set_pos(tabs_, 0, 64); lv_obj_set_size(tabs_, 800, 416);
  lv_tabview_set_tab_bar_size(tabs_, 45);
  auto *pumps = lv_tabview_add_tab(tabs_, "Дозирование");
  auto *motion = lv_tabview_add_tab(tabs_, "Привод");
  auto *cal = lv_tabview_add_tab(tabs_, "Калибровка");
  auto *link = lv_tabview_add_tab(tabs_, "USB / Журнал");
  for(auto *tab : {pumps, motion, cal, link}) lv_obj_set_style_pad_all(tab, 12, 0);
  for(int p = 1; p <= 2; ++p) {
    int x = (p - 1) * 388;
    label(pumps, p == 1 ? "НАСОС 1 / D8" : "НАСОС 2 / D10", x, 0, 350);
    label(pumps, "Порция, мл", x, 38, 200);
    volume_[p-1].create(pumps, x, 70, 1, 1000, 50);
    button(pumps, "Налить", x, 135, 230, Action::Dose, p);
    button(pumps, "Вкл.", x, 198, 110, Action::PumpOn, p);
    button(pumps, "Выкл.", x + 120, 198, 110, Action::PumpOff, p);
  }
  label(pumps, "Следующий насос запускайте после завершения текущей порции.", 0, 266, 755);
  label(motion, "Количество микрошагов", 0, 0, 755);
  steps_.create(motion, 0, 42, 1, 650, 10);
  button(motion, "Вверх", 245, 42, 140, Action::Move, 1);
  button(motion, "Вниз", 400, 42, 140, Action::Move, -1);
  button(motion, "Найти верхний ноль", 0, 110, 265, Action::Home);
  button(motion, "Управлять энкодером", 285, 110, 245, Action::Encoder);
  label(motion, "Во время поиска нуля и движения команда STOP не обрабатывается.\nДля немедленной остановки привода отключите силовое питание.", 0, 190, 755);
  pump_ = lv_dropdown_create(cal); lv_dropdown_set_options(pump_, "Насос 1\nНасос 2");
  lv_dropdown_set_symbol(pump_, "v");
  lv_obj_set_style_text_font(pump_, &mixer_font_16, 0);
  lv_obj_set_style_text_font(lv_dropdown_get_list(pump_), &mixer_font_16, 0);
  lv_obj_set_pos(pump_, 0, 0); lv_obj_set_size(pump_, 170, 44);
  button(cal, "Прокачка", 185, 0, 135, Action::Prime);
  button(cal, "20 сек", 335, 0, 125, Action::Measure);
  button(cal, "50 мл", 475, 0, 120, Action::CheckDose);
  label(cal, "Масса воды после замера 20 секунд, г", 0, 65, 750);
  grams_.create(cal, 0, 100, 1, 99999, 2730, 2);
  button(cal, "Сохранить", 245, 100, 190, Action::SaveRate);
  label(cal, "Общий уровень — налив выбранным насосом", 0, 162, 750);
  button(cal, "Начать уровни", 0, 190, 225, Action::HeightStart);
  button(cal, "Точка и ещё 5 мл", 240, 190, 225, Action::HeightMark);
  button(cal, "Завершить уровни", 480, 190, 245, Action::HeightStop);
  button(cal, "Показать уровни", 0, 252, 225, Action::HeightShow);
  button(cal, "Стереть уровни", 240, 252, 225, Action::HeightClear);
  label(link, "USB-порт (Linux)", 0, 0, 240);
  port_ = lv_textarea_create(link); lv_obj_set_pos(port_, 0, 34); lv_obj_set_size(port_, 290, 46);
  lv_textarea_set_one_line(port_, true); lv_textarea_set_max_length(port_, 127);
  lv_textarea_set_accepted_chars(port_, "/abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-.");
  lv_textarea_set_text(port_, "/dev/ttyACM0");
  button(link, "Подключить", 305, 34, 180, Action::Connect, 0, false);
  button(link, "Отключить", 500, 34, 230, Action::Disconnect, 0, false);
  connection_ = label(link, "Arduino не подключена", 0, 94, 750);
  lv_label_set_long_mode(connection_, LV_LABEL_LONG_DOT);
  status_ = label(link, "Данные о состоянии пока не получены", 0, 125, 750);
  log_ = lv_label_create(link); lv_obj_set_pos(log_, 0, 205); lv_obj_set_width(log_, 750);
  lv_obj_set_style_text_font(log_, &mixer_font_16, 0);
  lv_label_set_text(log_, "No replies yet.");
  createRecipePages(screen);
  back_ = button(screen, LV_SYMBOL_LEFT, 16, 8, 48, Action::MainMenu, 0, false);
  lv_obj_set_style_text_font(lv_obj_get_child(back_, 0), &lv_font_montserrat_28, 0);
  statusButton_ = button(screen, "Состояние", 80, 8, 170, Action::Status, 0, false);
  stopButton_ = button(screen, "Стоп", 620, 8, 164, Action::Stop, 0, false);
  lv_obj_set_style_bg_color(stopButton_, lv_color_hex(0xBA384B), 0);
  setConnection(false, false, "USB disconnected");
  showMain();
}

void MixerUi::clicked(lv_event_t *e) {
  auto *b = static_cast<Binding *>(lv_event_get_user_data(e));
  b->ui->dispatch(b->action, b->pump);
}
void MixerUi::dispatch(Action a, int p) {
  if(a == Action::MainMenu) { showMain(); return; }
  if(a == Action::Service) {
    if(cooking_) { showNotice("Сначала завершите или отмените приготовление"); return; }
    showPage(tabs_); return;
  }
  if(a == Action::Recipes) { showPage(catalog_); updateRecipeDetails(); return; }
  if(a == Action::NewRecipe || a == Action::EditRecipe) {
    if(cooking_) { showNotice("Сначала завершите или отмените приготовление"); return; }
    const Recipe *r = a == Action::EditRecipe ? selectedRecipe() : nullptr;
    if(a == Action::EditRecipe && !r) return;
    editingId_ = r ? r->id : 0;
    lv_dropdown_set_selected(firstPump_, r ? r->firstPump - 1 : 0);
    lv_textarea_set_text(recipeName_, r ? r->name.c_str() : "");
    char number[12];
    snprintf(number, sizeof(number), "%u", r ? r->ml1 : 50);
    lv_textarea_set_text(recipeMl1_.object(), number);
    snprintf(number, sizeof(number), "%u", r ? r->ml2 : 50);
    lv_textarea_set_text(recipeMl2_.object(), number);
    lv_label_set_text(editorMessage_, "0 мл — не использовать этот насос");
    showPage(editor_); return;
  }
  if(a == Action::SaveRecipe) {
    if(cooking_ || !recipeHandler_) return;
    Recipe r; float ml1, ml2;
    r.id = editingId_; r.name = lv_textarea_get_text(recipeName_);
    auto first = r.name.find_first_not_of(' '), last = r.name.find_last_not_of(' ');
    r.name = first == std::string::npos ? "" : r.name.substr(first, last - first + 1);
    if(!recipeMl1_.value(ml1) || !recipeMl2_.value(ml2)) {
      lv_label_set_text(editorMessage_, "Объёмы должны быть от 0 до 1000 мл"); return;
    }
    r.ml1 = ml1; r.ml2 = ml2;
    r.firstPump = lv_dropdown_get_selected(firstPump_) + 1;
    if(!validRecipe(r)) {
      lv_label_set_text(editorMessage_, "Введите название и ненулевой объём хотя бы одного насоса"); return;
    }
    recipeHandler_(recipeContext_, RecipeAction::Save, r); return;
  }
  if(a == Action::RemoveRecipe || a == Action::PrepareRecipe) {
    auto *r = selectedRecipe();
    if(!r || cooking_ || !recipeHandler_) return;
    if(a == Action::PrepareRecipe && !recipeReady_) {
      setRecipeMessage("Для приготовления нужна готовая Arduino с поддержкой рецептов"); return;
    }
    pendingRecipe_ = *r;
    pendingRecipeAction_ = a == Action::RemoveRecipe ? RecipeAction::Remove : RecipeAction::Prepare;
    auto *box = lv_msgbox_create(nullptr);
    lv_obj_set_style_text_font(box, &mixer_font_16, 0);
    lv_msgbox_add_title(box, a == Action::RemoveRecipe ? "Удалить рецепт?" : "Начать приготовление?");
    char details[240];
    snprintf(details, sizeof(details), "%s\nНасос 1: %u мл; насос 2: %u мл\nВсего: %u мл. Первым: насос %u.%s", r->name.c_str(), r->ml1, r->ml2, r->ml1 + r->ml2, r->firstPump,
      a == Action::PrepareRecipe ? "\nПоставьте подходящую ёмкость. Проверьте жидкости в трубках." : "");
    lv_msgbox_add_text(box, details); cancelButton(box);
    auto *ok = lv_msgbox_add_footer_button(box, "Подтвердить");
    lv_obj_set_style_text_font(ok, &mixer_font_16, 0); lv_obj_set_user_data(ok, box);
    lv_obj_add_event_cb(ok, recipeConfirmed, LV_EVENT_CLICKED, this); return;
  }
  float value = 0;
  bool valid = true;
  if(a == Action::Dose) valid = volume_[p-1].value(value);
  if(a == Action::Move) { valid = steps_.value(value); value *= p; }
  if(a == Action::SaveRate) valid = grams_.value(value);
  if(!valid) {
    showNotice(a == Action::Dose ? "Введите объём: 1-1000 мл" :
              a == Action::Move ? "Введите шаги: 1-650" : "Введите массу: 0.01-999.99 г");
    return;
  }
  if(a == Action::Status) showTab(3);
  if(a == Action::Prime || a == Action::Measure || a == Action::CheckDose ||
     a == Action::SaveRate || a == Action::HeightStart) p = lv_dropdown_get_selected(pump_) + 1;
  pending_ = {a, p, value};
  if(a == Action::Home || a == Action::HeightStart || a == Action::HeightClear || a == Action::SaveRate) {
    auto *box = lv_msgbox_create(nullptr);
    lv_obj_set_style_text_font(box, &mixer_font_16, 0);
    lv_msgbox_add_title(box, "Подтверждение");
    lv_msgbox_add_text(box, a == Action::Home ? "Начать поиск верхнего концевика?" :
      a == Action::HeightStart ? "Стереть общую таблицу уровня, найти ноль и налить 5 мл выбранным насосом?" :
      a == Action::HeightClear ? "Стереть общую таблицу уровня из EEPROM?" : "Записать коэффициент насоса в EEPROM?");
    cancelButton(box);
    auto *ok = lv_msgbox_add_footer_button(box, "Выполнить");
    lv_obj_set_style_text_font(ok, &mixer_font_16, 0);
    lv_obj_set_user_data(ok, box);
    lv_obj_add_event_cb(ok, confirmed, LV_EVENT_CLICKED, this);
    return;
  }
  request(pending_);
}
void MixerUi::confirmed(lv_event_t *e) {
  auto *self = static_cast<MixerUi *>(lv_event_get_user_data(e));
  self->request(self->pending_);
  lv_msgbox_close(static_cast<lv_obj_t *>(lv_obj_get_user_data(lv_event_get_target_obj(e))));
}
void MixerUi::request(const Command &c) {
  if(handler_) handler_(context_, c);
}
void MixerUi::setConnection(bool connected, bool working, const char *text) {
  (void)connected;
  working_ = working;
  lv_label_set_text(connection_, text);
  for(unsigned i = 0; i < controlCount_; ++i) {
    if(working_) lv_obj_remove_state(controls_[i], LV_STATE_DISABLED);
    else lv_obj_add_state(controls_[i], LV_STATE_DISABLED);
  }
}
void MixerUi::setStatus(const char *text) { lv_label_set_text(status_, text); }
void MixerUi::setLog(const char *text) { lv_label_set_text(log_, text); }
void MixerUi::setPort(const char *text) { lv_textarea_set_text(port_, text); }
const char *MixerUi::port() const { return lv_textarea_get_text(port_); }
void MixerUi::showTab(unsigned i) { showPage(tabs_); lv_tabview_set_active(tabs_, i, LV_ANIM_OFF); }

void MixerUi::showPage(lv_obj_t *page) {
  visiblePage_ = page;
  for(auto *p : {main_, catalog_, editor_, tabs_}) {
    if(p == page) lv_obj_remove_flag(p, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(p, LV_OBJ_FLAG_HIDDEN);
  }
  updateNavigation();
}
void MixerUi::updateNavigation() {
  if(!back_) return;
  auto visible = [](lv_obj_t *obj, bool show) {
    if(show) lv_obj_remove_flag(obj, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
  };
  visible(back_, visiblePage_ != main_);
  visible(statusButton_, visiblePage_ == tabs_);
  visible(stopButton_, cooking_ || calibrating_);
}
void MixerUi::setCalibrating(bool active) { calibrating_ = active; updateNavigation(); }
void MixerUi::showNotice(const char *message) {
  auto *box = lv_msgbox_create(nullptr);
  lv_obj_set_style_text_font(box, &mixer_font_16, 0);
  lv_msgbox_add_text(box, message); cancelButton(box, "Понятно");
}
void MixerUi::showMain() { showPage(main_); }
void MixerUi::showRecipes() { showPage(catalog_); updateRecipeDetails(); }
void MixerUi::createRecipePages(lv_obj_t *screen) {
  auto page = [screen]() {
    auto *p = lv_obj_create(screen); lv_obj_set_pos(p, 0, 64); lv_obj_set_size(p, 800, 416);
    lv_obj_set_style_text_font(p, &mixer_font_16, 0); return p;
  };
  main_ = page(); catalog_ = page(); editor_ = page();
  lv_obj_set_pos(main_, 0, 0); lv_obj_set_size(main_, 800, 480);
  lv_obj_set_style_pad_all(main_, 16, 0);
  lv_obj_set_style_border_width(main_, 0, 0);
  lv_obj_set_style_radius(main_, 0, 0);
  lv_obj_set_style_bg_color(main_, lv_color_hex(0x101827), 0);
  lv_obj_remove_flag(main_, LV_OBJ_FLAG_SCROLLABLE);
  const char *titles[] = {"Приготовить напиток", "Добавить рецепт", "Сервисный режим"};
  const Action actions[] = {Action::Recipes, Action::NewRecipe, Action::Service};
  const uint32_t colors[] = {0x163A42, 0x3E3045, 0x293C53};
  for(unsigned i = 0; i < 3; ++i) {
    auto *card = button(main_, titles[i], i * 260, 0, 248, actions[i], 0, false);
    lv_obj_set_height(card, 448);
    lv_obj_set_style_radius(card, 18, 0);
    lv_obj_set_style_pad_all(card, 0, 0);
    lv_obj_set_style_shadow_width(card, 0, 0);
    lv_obj_set_style_bg_color(card, lv_color_hex(colors[i]), 0);
    // Future photos can be assigned as the button's LVGL background image;
    // the caption stays above the image and remains part of the same hit target.
    auto *caption = lv_obj_get_child(card, 0);
    lv_obj_set_width(caption, 224);
    lv_obj_set_style_text_align(caption, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(caption, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_color(caption, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(caption, LV_OPA_60, 0);
    lv_obj_set_style_radius(caption, 8, 0);
    lv_obj_set_style_pad_ver(caption, 14, 0);
    lv_obj_align(caption, LV_ALIGN_BOTTOM_MID, 0, -20);
  }
  label(catalog_, "Выберите рецепт", 0, 0, 270);
  recipeList_ = lv_dropdown_create(catalog_); lv_obj_set_pos(recipeList_, 0, 32); lv_obj_set_size(recipeList_, 735, 48);
  lv_dropdown_set_symbol(recipeList_, "v");
  lv_obj_set_style_text_font(recipeList_, &mixer_font_16, 0);
  lv_obj_set_style_text_font(lv_dropdown_get_list(recipeList_), &mixer_font_16, 0);
  lv_obj_add_event_cb(recipeList_, recipeSelected, LV_EVENT_VALUE_CHANGED, this);
  recipeDetails_ = label(catalog_, "Пока нет рецептов. Добавьте первый с главного экрана.", 0, 94, 740);
  recipeMessage_ = label(catalog_, "", 0, 145, 740);
  progress_ = lv_bar_create(catalog_); lv_obj_set_pos(progress_, 0, 202); lv_obj_set_size(progress_, 735, 12);
  recipeButtons_[0] = button(catalog_, "Приготовить", 0, 240, 235, Action::PrepareRecipe, 0, false);
  recipeButtons_[1] = button(catalog_, "Изменить", 250, 240, 235, Action::EditRecipe, 0, false);
  recipeButtons_[2] = button(catalog_, "Удалить", 500, 240, 235, Action::RemoveRecipe, 0, false);
  label(editor_, "Название", 0, 0, 740);
  recipeName_ = lv_textarea_create(editor_); lv_obj_set_pos(recipeName_, 0, 28); lv_obj_set_size(recipeName_, 735, 48);
  lv_textarea_set_one_line(recipeName_, true); lv_textarea_set_max_length(recipeName_, 32);
  label(editor_, "Насос 1, мл", 0, 90, 330); label(editor_, "Насос 2, мл", 385, 90, 330);
  recipeMl1_.create(editor_, 0, 122, 0, 1000, 50);
  recipeMl2_.create(editor_, 385, 122, 0, 1000, 50);
  label(editor_, "Первым наливает:", 0, 195, 320);
  firstPump_ = lv_dropdown_create(editor_); lv_obj_set_pos(firstPump_, 385, 183); lv_obj_set_size(firstPump_, 350, 44);
  lv_dropdown_set_options(firstPump_, "Насос 1\nНасос 2"); lv_dropdown_set_symbol(firstPump_, "v");
  lv_obj_set_style_text_font(firstPump_, &mixer_font_16, 0);
  lv_obj_set_style_text_font(lv_dropdown_get_list(firstPump_), &mixer_font_16, 0);
  editorMessage_ = label(editor_, "0 мл — не использовать этот насос", 0, 240, 735);
  button(editor_, "Сохранить рецепт", 0, 280, 735, Action::SaveRecipe, 0, false);
  setRecipes({});
}
void MixerUi::setRecipes(const std::vector<Recipe> &r) {
  uint32_t previous = selectedRecipe() ? selectedRecipe()->id : 0;
  recipes_ = r;
  std::string options;
  unsigned index = 0;
  for(unsigned i = 0; i < recipes_.size(); ++i) {
    if(i) options += '\n';
    options += recipes_[i].name;
    if(recipes_[i].id == previous) index = i;
  }
  lv_dropdown_set_options(recipeList_, options.empty() ? "Нет рецептов" : options.c_str());
  lv_dropdown_set_selected(recipeList_, index); updateRecipeDetails();
}
const Recipe *MixerUi::selectedRecipe() const {
  if(!recipeList_) return nullptr;
  unsigned selected = lv_dropdown_get_selected(recipeList_);
  return selected < recipes_.size() ? &recipes_[selected] : nullptr;
}
void MixerUi::updateRecipeDetails() {
  auto *r = selectedRecipe();
  char text[180];
  if(r) snprintf(text, sizeof(text), "Насос 1: %u мл    Насос 2: %u мл    Всего: %u мл\nПервым: насос %u. Калибровка уровня общая", r->ml1, r->ml2, r->ml1 + r->ml2, r->firstPump);
  else snprintf(text, sizeof(text), "Пока нет рецептов. Добавьте первый с главного экрана.");
  lv_label_set_text(recipeDetails_, text);
  for(unsigned i = 0; i < 3; ++i) {
    if(r && !cooking_ && (i != 0 || recipeReady_)) lv_obj_remove_state(recipeButtons_[i], LV_STATE_DISABLED);
    else lv_obj_add_state(recipeButtons_[i], LV_STATE_DISABLED);
  }
}
void MixerUi::recipeSelected(lv_event_t *e) { static_cast<MixerUi *>(lv_event_get_user_data(e))->updateRecipeDetails(); }
void MixerUi::recipeConfirmed(lv_event_t *e) {
  auto *self = static_cast<MixerUi *>(lv_event_get_user_data(e));
  if(!self->cooking_ && self->recipeHandler_) self->recipeHandler_(self->recipeContext_, self->pendingRecipeAction_, self->pendingRecipe_);
  lv_msgbox_close(static_cast<lv_obj_t *>(lv_obj_get_user_data(lv_event_get_target_obj(e))));
}
void MixerUi::setRecipeHandler(RecipeHandler h, void *context) { recipeHandler_ = h; recipeContext_ = context; }
void MixerUi::setRecipeMessage(const char *message) { lv_label_set_text(recipeMessage_, message); lv_label_set_text(editorMessage_, message); }
void MixerUi::setCooking(bool ready, bool busy, const char *message, int percent) {
  recipeReady_ = ready; cooking_ = busy;
  lv_label_set_text(recipeMessage_, message); lv_bar_set_value(progress_, percent, LV_ANIM_OFF);
  updateRecipeDetails();
  updateNavigation();
}
}
