#include "MixerUi.h"
#include <stdio.h>
#include <initializer_list>
LV_FONT_DECLARE(mixer_font_16);
LV_FONT_DECLARE(mixer_font_title);
LV_IMAGE_DECLARE(menu_cocktail);
LV_IMAGE_DECLARE(menu_recipe);
LV_IMAGE_DECLARE(menu_care);
LV_IMAGE_DECLARE(menu_service);

namespace smart_mixer {
namespace {
// Fixed-size scrollable panel; its text label grows inside and never overlaps neighbours.
lv_obj_t *scrollBox(lv_obj_t *parent, int x, int y, int width, int height, lv_obj_t **text) {
  auto *box = lv_obj_create(parent);
  lv_obj_set_pos(box, x, y); lv_obj_set_size(box, width, height);
  lv_obj_set_style_pad_all(box, 8, 0);
  lv_obj_set_style_radius(box, 6, 0);
  lv_obj_set_style_bg_color(box, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_scroll_dir(box, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(box, LV_SCROLLBAR_MODE_AUTO);
  *text = lv_label_create(box);
  lv_obj_set_width(*text, lv_pct(100));
  lv_label_set_long_mode(*text, LV_LABEL_LONG_WRAP);
  return box;
}
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
    pumpTitles_[p-1] = label(pumps, "", x, 0, 370);
    lv_label_set_long_mode(pumpTitles_[p-1], LV_LABEL_LONG_DOT);
    label(pumps, "Порция, мл", x, 38, 200);
    volume_[p-1].create(pumps, x, 70, 1, 1000, 50);
    button(pumps, "Налить", x, 135, 230, Action::Dose, p);
    button(pumps, "Вкл.", x, 198, 110, Action::PumpOn, p);
    button(pumps, "Выкл.", x + 120, 198, 110, Action::PumpOff, p);
  }
  label(pumps, "Следующий насос запускайте после завершения текущей порции.", 0, 266, 755);
  // Feedback right here: Arduino may refuse a dose (no homing, pump not calibrated).
  dosingStatus_ = label(pumps, "", 0, 294, 755);
  lv_label_set_long_mode(dosingStatus_, LV_LABEL_LONG_DOT);
  dosingReply_ = label(pumps, "", 0, 320, 755);
  lv_label_set_long_mode(dosingReply_, LV_LABEL_LONG_DOT);
  label(motion, "Количество микрошагов", 0, 0, 755);
  steps_.create(motion, 0, 42, 1, 1000, 10);
  button(motion, "Вверх", 245, 42, 140, Action::Move, 1);
  button(motion, "Вниз", 400, 42, 140, Action::Move, -1);
  button(motion, "Найти верхний ноль", 0, 110, 265, Action::Home);
  button(motion, "Управлять энкодером", 285, 110, 245, Action::Encoder);
  button(motion, "Отключить энкодер", 545, 110, 210, Action::EncoderOff);
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
  // What Arduino has in EEPROM right now (refreshed by "Состояние" and level replies).
  eepromInfo_ = label(cal, "EEPROM: нажмите Состояние", 450, 98, 305);
  label(cal, "Общий уровень — налив выбранным насосом", 0, 162, 750);
  button(cal, "Начать уровни", 0, 190, 225, Action::HeightStart);
  button(cal, "Точка и ещё 5 мл", 240, 190, 225, Action::HeightMark);
  button(cal, "Завершить уровни", 480, 190, 245, Action::HeightStop);
  button(cal, "Показать уровни", 0, 252, 225, Action::HeightShow);
  button(cal, "Стереть уровни", 240, 252, 225, Action::HeightClear);
  // Shows whether each level point was saved or rejected by Arduino.
  calReply_ = label(cal, "", 0, 312, 755);
  lv_label_set_long_mode(calReply_, LV_LABEL_LONG_DOT);
  label(link, "USB-порт (Linux)", 0, 0, 240);
  port_ = lv_textarea_create(link); lv_obj_set_pos(port_, 0, 34); lv_obj_set_size(port_, 290, 46);
  lv_textarea_set_one_line(port_, true); lv_textarea_set_max_length(port_, 127);
  lv_textarea_set_accepted_chars(port_, "/abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-.");
  lv_textarea_set_text(port_, "/dev/ttyACM0");
  button(link, "Подключить", 305, 34, 180, Action::Connect, 0, false);
  button(link, "Отключить", 500, 34, 230, Action::Disconnect, 0, false);
  connection_ = label(link, "Arduino не подключена", 0, 92, 750);
  lv_label_set_long_mode(connection_, LV_LABEL_LONG_DOT);
  label(link, "Состояние", 0, 120, 280);
  label(link, "Журнал (> отправлено, < ответ Arduino)", 300, 120, 460);
  statusBox_ = scrollBox(link, 0, 146, 288, 186, &status_);
  logBox_ = scrollBox(link, 300, 146, 460, 186, &log_);
  lv_label_set_text(status_, "Данные о состоянии пока не получены");
  lv_label_set_text(log_, "No replies yet.");
  lv_obj_remove_flag(link, LV_OBJ_FLAG_SCROLLABLE);
  createRecipePages(screen);
  createCarePage(screen);
  back_ = button(screen, LV_SYMBOL_LEFT, 16, 8, 48, Action::MainMenu, 0, false);
  lv_obj_set_style_text_font(lv_obj_get_child(back_, 0), &lv_font_montserrat_28, 0);
  statusButton_ = button(screen, "Состояние", 80, 8, 170, Action::Status, 0, false);
  stopButton_ = button(screen, "Стоп", 620, 8, 164, Action::Stop, 0, false);
  lv_obj_set_style_bg_color(stopButton_, lv_color_hex(0xBA384B), 0);
  setConnection(false, false, "USB disconnected");
  refreshChannels();
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
  if(a == Action::Care) {
    if(cooking_) { showNotice("Сначала завершите или отмените приготовление"); return; }
    showPage(care_); return;
  }
  if(a == Action::SaveChannelName) {
    if(!channelHandler_) return;
    std::string name = lv_textarea_get_text(channelName_);
    auto first = name.find_first_not_of(' '), last = name.find_last_not_of(' ');
    name = first == std::string::npos ? "" : name.substr(first, last - first + 1);
    for(unsigned char c : name) if(c < 32 || c == 127) { setChannelMessage("Недопустимые символы в названии"); return; }
    channelHandler_(channelContext_, static_cast<int>(lv_dropdown_get_selected(careChannel_)) + 1, name);
    return;
  }
  if(a == Action::CalibratePrime || a == Action::PrimeArrived || a == Action::CarePrime ||
     a == Action::WashSolution || a == Action::WashWater) {
    if(cooking_) { showNotice("Сначала завершите или отмените приготовление"); return; }
    int pump = static_cast<int>(lv_dropdown_get_selected(careChannel_)) + 1;
    if(a == Action::CalibratePrime) { request({Action::Prime, pump, 0}); return; }
    if(a == Action::PrimeArrived) { request({Action::PrimeMeasured, pump, 0}); return; }
    float line = primeTimes_[pump - 1];
    if(line <= 0) { showNotice("Сначала откалибруйте прокачку этого насоса"); return; }
    if(a == Action::CarePrime) { request({Action::RunFor, pump, line}); return; }
    float passes = 0;
    if(!washPasses_.value(passes)) { showNotice("Число прогонов трассы: 1-10"); return; }
    float seconds = line * passes;
    if(seconds > 600) { showNotice("Промывка дольше 600 с. Уменьшите число прогонов"); return; }
    pending_ = {Action::RunFor, pump, seconds};
    char text[320];
    snprintf(text, sizeof(text), "%s\nОпустите входную трубку в %s, под выход поставьте пустую ёмкость.\n"
      "Насос проработает %.1f с (%.0f x %.1f с трассы).%s", pumpName(pump).c_str(),
      a == Action::WashSolution ? "моющий раствор" : "чистую воду", seconds, passes, line,
      a == Action::WashSolution ? "\nПосле раствора промойте канал чистой водой." : "");
    askConfirmation(a == Action::WashSolution ? "Промывка раствором" : "Промывка водой", text);
    return;
  }
  if(a == Action::NewRecipe || a == Action::EditRecipe) {
    if(cooking_) { showNotice("Сначала завершите или отмените приготовление"); return; }
    const Recipe *r = a == Action::EditRecipe ? selectedRecipe() : nullptr;
    if(a == Action::EditRecipe && !r) return;
    editingId_ = r ? r->id : 0;
    lv_dropdown_set_selected(firstPump_, r ? r->firstPump - 1 : 0);
    lv_dropdown_set_selected(recipeMode_, r && !r->layered ? 1 : 0);
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
    r.layered = lv_dropdown_get_selected(recipeMode_) == 0;
    if(!validRecipe(r)) {
      lv_label_set_text(editorMessage_, "Введите название и ненулевой объём хотя бы одного насоса"); return;
    }
    recipeHandler_(recipeContext_, RecipeAction::Save, r); return;
  }
  if(a == Action::RemoveRecipe || a == Action::PrepareRecipe) {
    auto *r = selectedRecipe();
    if(!r || cooking_ || !recipeHandler_) return;
    if(a == Action::PrepareRecipe) {
      // The adapter knows the hardware state; it explains a refusal instead of a grey button.
      recipeRejected_ = false;
      recipeHandler_(recipeContext_, RecipeAction::Check, *r);
      if(recipeRejected_) return;
    }
    pendingRecipe_ = *r;
    pendingRecipeAction_ = a == Action::RemoveRecipe ? RecipeAction::Remove : RecipeAction::Prepare;
    auto *box = lv_msgbox_create(nullptr);
    lv_obj_set_style_text_font(box, &mixer_font_16, 0);
    lv_msgbox_add_title(box, a == Action::RemoveRecipe ? "Удалить рецепт?" : "Начать приготовление?");
    char details[400];
    snprintf(details, sizeof(details), "%s\n%s: %u мл\n%s: %u мл\nВсего: %u мл. %s, первым — насос %u.%s", r->name.c_str(),
      pumpName(1).c_str(), r->ml1, pumpName(2).c_str(), r->ml2, r->ml1 + r->ml2,
      r->layered ? "Слоями" : "Смешать у края", r->firstPump,
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
              a == Action::Move ? "Введите шаги: 1-1000" : "Введите массу: 0.01-999.99 г");
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
  if(dosingStatus_) lv_label_set_text(dosingStatus_, text);
  if(careStatus_) lv_label_set_text(careStatus_, working ? text :
    "Сначала: Сервисный режим, USB / Журнал — Подключить; затем Привод — Найти верхний ноль");
  for(unsigned i = 0; i < controlCount_; ++i) {
    if(working_) lv_obj_remove_state(controls_[i], LV_STATE_DISABLED);
    else lv_obj_add_state(controls_[i], LV_STATE_DISABLED);
  }
}
void MixerUi::setStatus(const char *text) { lv_label_set_text(status_, text); }
void MixerUi::setLog(const char *text) {
  // Follow new lines only while the reader is at the end; keep position after scrolling up.
  lv_obj_update_layout(logBox_);
  bool atEnd = lv_obj_get_scroll_bottom(logBox_) <= 4;
  lv_label_set_text(log_, text);
  if(atEnd) { lv_obj_update_layout(logBox_); lv_obj_scroll_to_y(logBox_, 1000000, LV_ANIM_OFF); }
  if(!careReply_ || !dosingReply_ || !calReply_) return;
  // Last Arduino reply, so priming/washing results are visible without the service log.
  std::string log = text, last;
  size_t end = log.size();
  while(end > 0) {
    size_t start = log.rfind('\n', end - 1);
    start = start == std::string::npos ? 0 : start + 1;
    if(log.compare(start, 2, "< ") == 0) { last = log.substr(start + 2, end - start - 2); break; }
    if(start == 0) break;
    end = start - 1;
  }
  lv_label_set_text(careReply_, last.empty() ? "" : ("Arduino: " + last).c_str());
  lv_label_set_text(dosingReply_, last.empty() ? "" : ("Arduino: " + last).c_str());
  lv_label_set_text(calReply_, last.empty() ? "" : ("Arduino: " + last).c_str());
}
void MixerUi::setPort(const char *text) { lv_textarea_set_text(port_, text); }
const char *MixerUi::port() const { return lv_textarea_get_text(port_); }
void MixerUi::showTab(unsigned i) { showPage(tabs_); lv_tabview_set_active(tabs_, i, LV_ANIM_OFF); }

void MixerUi::showPage(lv_obj_t *page) {
  visiblePage_ = page;
  for(auto *p : {main_, catalog_, editor_, care_, tabs_}) {
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
  lv_obj_set_style_pad_all(main_, 0, 0);
  lv_obj_set_style_border_width(main_, 0, 0);
  lv_obj_set_style_radius(main_, 0, 0);
  lv_obj_set_style_bg_color(main_, lv_color_hex(0x101827), 0);
  lv_obj_remove_flag(main_, LV_OBJ_FLAG_SCROLLABLE);
  auto *brand = label(main_, "Smart Mixer", 24, 20, 300);
  lv_obj_set_style_text_font(brand, &mixer_font_title, 0);
  lv_obj_set_style_text_color(brand, lv_color_hex(0xFFFFFF), 0);
  auto *hint = label(main_, "Выберите действие", 476, 26, 300);
  lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_RIGHT, 0);
  lv_obj_set_style_text_color(hint, lv_color_hex(0x8FA3BF), 0);
  // One large primary card on the left, three secondary cards on the right.
  // Illustrations are generated by scripts/make_menu_images.py (menu_images.c).
  struct Card { const char *title, *subtitle; Action action; uint32_t top, bottom; const lv_image_dsc_t *image; };
  const Card cards[] = {
    {"Приготовить\nнапиток", "Выбрать рецепт и налить", Action::Recipes, 0x1A4A52, 0x10303A, &menu_cocktail},
    {"Добавить рецепт", "Название, объёмы, слоями или смешать", Action::NewRecipe, 0x3C3150, 0x281F36, &menu_recipe},
    {"Прокачка и промывка", "Калибровка трассы, промывка каналов", Action::Care, 0x274536, 0x182C22, &menu_care},
    {"Сервисный режим", "Дозирование, привод, калибровка, USB", Action::Service, 0x2A3F5C, 0x1A283C, &menu_service},
  };
  for(unsigned i = 0; i < 4; ++i) {
    const bool primary = i == 0;
    auto *card = primary ? button(main_, cards[i].title, 16, 68, 312, cards[i].action, 0, false)
                         : button(main_, cards[i].title, 340, 68 + (i - 1) * 134, 444, cards[i].action, 0, false);
    lv_obj_set_height(card, primary ? 396 : 124);
    lv_obj_set_style_radius(card, 20, 0);
    lv_obj_set_style_pad_all(card, 0, 0);
    lv_obj_set_style_shadow_width(card, 0, 0);
    // Solid fills: gradients band visibly at the panel's 16-bit colour depth.
    lv_obj_set_style_bg_color(card, lv_color_hex(cards[i].top), 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_border_color(card, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_border_opa(card, LV_OPA_10, 0);
    lv_obj_set_style_bg_color(card, lv_color_hex(cards[i].bottom), LV_STATE_PRESSED);
    // Child 0 stays the title label: tests and adapters find menu buttons by it.
    auto *caption = lv_obj_get_child(card, 0);
    lv_obj_set_style_text_font(caption, &mixer_font_title, 0);
    lv_obj_set_style_text_color(caption, lv_color_hex(0xFFFFFF), 0);
    auto *subtitle = lv_label_create(card);
    lv_label_set_text(subtitle, cards[i].subtitle);
    lv_obj_set_style_text_color(subtitle, lv_color_hex(0xD6E0EA), 0);
    lv_obj_set_style_text_opa(subtitle, LV_OPA_80, 0);
    auto *image = lv_image_create(card);
    lv_image_set_src(image, cards[i].image);
    lv_obj_remove_flag(image, LV_OBJ_FLAG_CLICKABLE);
    if(primary) {
      lv_obj_align(image, LV_ALIGN_TOP_MID, 0, 52);
      lv_obj_set_style_text_align(caption, LV_TEXT_ALIGN_CENTER, 0);
      lv_obj_align(caption, LV_ALIGN_BOTTOM_MID, 0, -64);
      lv_obj_set_style_text_align(subtitle, LV_TEXT_ALIGN_CENTER, 0);
      lv_obj_align(subtitle, LV_ALIGN_BOTTOM_MID, 0, -28);
    } else {
      lv_obj_align(image, LV_ALIGN_LEFT_MID, 16, 0);
      lv_obj_align(caption, LV_ALIGN_TOP_LEFT, 118, 26);
      lv_obj_set_width(subtitle, 314);
      lv_obj_align(subtitle, LV_ALIGN_TOP_LEFT, 118, 64);
    }
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
  editorPumpLabels_[0] = label(editor_, "", 0, 90, 370);
  editorPumpLabels_[1] = label(editor_, "", 385, 90, 350);
  for(auto *l : editorPumpLabels_) lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
  recipeMl1_.create(editor_, 0, 122, 0, 1000, 50);
  recipeMl2_.create(editor_, 385, 122, 0, 1000, 50);
  label(editor_, "Режим:", 0, 250, 320);
  // Created before firstPump_: tests and adapters treat the last dropdown as the pump order.
  recipeMode_ = lv_dropdown_create(editor_); lv_obj_set_pos(recipeMode_, 385, 238); lv_obj_set_size(recipeMode_, 350, 44);
  lv_dropdown_set_options(recipeMode_, "Слоями\nСмешать у края"); lv_dropdown_set_symbol(recipeMode_, "v");
  lv_obj_set_style_text_font(recipeMode_, &mixer_font_16, 0);
  lv_obj_set_style_text_font(lv_dropdown_get_list(recipeMode_), &mixer_font_16, 0);
  label(editor_, "Первым наливает:", 0, 195, 320);
  firstPump_ = lv_dropdown_create(editor_); lv_obj_set_pos(firstPump_, 385, 183); lv_obj_set_size(firstPump_, 350, 44);
  lv_dropdown_set_options(firstPump_, "Насос 1\nНасос 2"); lv_dropdown_set_symbol(firstPump_, "v");
  lv_obj_set_style_text_font(firstPump_, &mixer_font_16, 0);
  lv_obj_set_style_text_font(lv_dropdown_get_list(firstPump_), &mixer_font_16, 0);
  editorMessage_ = label(editor_, "0 мл — не использовать этот насос", 0, 292, 735);
  button(editor_, "Сохранить рецепт", 0, 322, 735, Action::SaveRecipe, 0, false);
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
  char text[300];
  if(r) snprintf(text, sizeof(text), "%s: %u мл    %s: %u мл    Всего: %u мл\n%s, первым — насос %u",
    pumpName(1).c_str(), r->ml1, pumpName(2).c_str(), r->ml2, r->ml1 + r->ml2,
    r->layered ? "Слоями: шток следует уровню" : "Смешать: налив у края рюмки", r->firstPump);
  else snprintf(text, sizeof(text), "Пока нет рецептов. Добавьте первый с главного экрана.");
  lv_label_set_text(recipeDetails_, text);
  for(unsigned i = 0; i < 3; ++i) {
    if(r && !cooking_) lv_obj_remove_state(recipeButtons_[i], LV_STATE_DISABLED);
    else lv_obj_add_state(recipeButtons_[i], LV_STATE_DISABLED);
  }
}
void MixerUi::recipeSelected(lv_event_t *e) { static_cast<MixerUi *>(lv_event_get_user_data(e))->updateRecipeDetails(); }
void MixerUi::recipeConfirmed(lv_event_t *e) {
  auto *self = static_cast<MixerUi *>(lv_event_get_user_data(e));
  if(!self->cooking_ && self->recipeHandler_) self->recipeHandler_(self->recipeContext_, self->pendingRecipeAction_, self->pendingRecipe_);
  lv_msgbox_close(static_cast<lv_obj_t *>(lv_obj_get_user_data(lv_event_get_target_obj(e))));
}
void MixerUi::rejectRecipe(const char *reason) {
  recipeRejected_ = true;
  auto *box = lv_msgbox_create(nullptr);
  lv_obj_set_style_text_font(box, &mixer_font_16, 0);
  lv_obj_set_width(box, 520);
  lv_msgbox_add_title(box, "Сейчас приготовить нельзя");
  lv_msgbox_add_text(box, reason);
  cancelButton(box, "Понятно");
  // Under the recipe list only the first line fits above the progress bar.
  std::string summary = reason;
  summary = summary.substr(0, summary.find('\n'));
  lv_label_set_text(recipeMessage_, summary.c_str());
}
void MixerUi::setRecipeHandler(RecipeHandler h, void *context) { recipeHandler_ = h; recipeContext_ = context; }
void MixerUi::setRecipeMessage(const char *message) { lv_label_set_text(recipeMessage_, message); lv_label_set_text(editorMessage_, message); }
void MixerUi::setCooking(bool ready, bool busy, const char *message, int percent) {
  recipeReady_ = ready; cooking_ = busy;
  lv_label_set_text(recipeMessage_, message); lv_bar_set_value(progress_, percent, LV_ANIM_OFF);
  updateRecipeDetails();
  updateNavigation();
}

void MixerUi::askConfirmation(const char *title, const char *text) {
  auto *box = lv_msgbox_create(nullptr);
  lv_obj_set_style_text_font(box, &mixer_font_16, 0);
  lv_msgbox_add_title(box, title);
  lv_msgbox_add_text(box, text);
  cancelButton(box);
  auto *ok = lv_msgbox_add_footer_button(box, "Выполнить");
  lv_obj_set_style_text_font(ok, &mixer_font_16, 0);
  lv_obj_set_user_data(ok, box);
  lv_obj_add_event_cb(ok, confirmed, LV_EVENT_CLICKED, this);
}
std::string MixerUi::pumpName(int pump) const {
  std::string name = pump == 1 ? "Насос 1" : "Насос 2";
  const std::string &ingredient = channels_[pump == 1 ? 0 : 1];
  return ingredient.empty() ? name : name + " — " + ingredient;
}
void MixerUi::createCarePage(lv_obj_t *screen) {
  care_ = lv_obj_create(screen); lv_obj_set_pos(care_, 0, 64); lv_obj_set_size(care_, 800, 416);
  lv_obj_set_style_text_font(care_, &mixer_font_16, 0);
  label(care_, "Канал", 0, 12, 100);
  careChannel_ = lv_dropdown_create(care_); lv_obj_set_pos(careChannel_, 70, 0); lv_obj_set_size(careChannel_, 300, 44);
  lv_dropdown_set_symbol(careChannel_, "v");
  lv_obj_set_style_text_font(careChannel_, &mixer_font_16, 0);
  lv_obj_set_style_text_font(lv_dropdown_get_list(careChannel_), &mixer_font_16, 0);
  lv_obj_add_event_cb(careChannel_, careChannelChanged, LV_EVENT_VALUE_CHANGED, this);
  channelName_ = lv_textarea_create(care_); lv_obj_set_pos(channelName_, 385, 0); lv_obj_set_size(channelName_, 175, 44);
  lv_textarea_set_one_line(channelName_, true); lv_textarea_set_max_length(channelName_, 24);
  lv_textarea_set_placeholder_text(channelName_, "Ингредиент");
  button(care_, "Сохранить", 570, 0, 165, Action::SaveChannelName, 0, false);
  lv_obj_set_height(lv_obj_get_child(care_, -1), 44);
  channelMessage_ = label(care_, "", 0, 50, 735);
  label(care_, "Калибровка трассы (один раз): трасса пустая, трубка в жидкости. Калибровать, затем Жидкость пошла, когда жидкость появится на выходе.", 0, 76, 735);
  button(care_, "Калибровать", 0, 124, 175, Action::CalibratePrime);
  button(care_, "Жидкость пошла", 185, 124, 200, Action::PrimeArrived);
  button(care_, "Прокачать", 395, 124, 165, Action::CarePrime);
  primeInfo_ = label(care_, "", 572, 128, 165);
  label(care_, "Промывка: сначала раствором, затем водой. Прогонов трассы:", 0, 186, 735);
  washPasses_.create(care_, 0, 216, 1, 10, 2);
  button(care_, "Раствором", 235, 216, 160, Action::WashSolution);
  button(care_, "Водой", 405, 216, 150, Action::WashWater);
  button(care_, "Остановить насос", 565, 216, 170, Action::Stop, 0, false);
  careStatus_ = label(care_, "", 0, 282, 735);
  lv_label_set_long_mode(careStatus_, LV_LABEL_LONG_DOT);
  careReply_ = label(care_, "", 0, 332, 735);
  lv_label_set_long_mode(careReply_, LV_LABEL_LONG_DOT);
}
void MixerUi::careChannelChanged(lv_event_t *e) {
  auto *self = static_cast<MixerUi *>(lv_event_get_user_data(e));
  lv_textarea_set_text(self->channelName_, self->channels_[lv_dropdown_get_selected(self->careChannel_) ? 1 : 0].c_str());
  self->setChannelMessage("");
  self->updatePrimeInfo();
}
void MixerUi::updatePrimeInfo() {
  float line = primeTimes_[lv_dropdown_get_selected(careChannel_) ? 1 : 0];
  char text[48];
  if(line > 0) snprintf(text, sizeof(text), "Трасса: %.1f с", line);
  else snprintf(text, sizeof(text), "Трасса: нет");
  lv_label_set_text(primeInfo_, text);
}
void MixerUi::setPrimeTimes(float pump1, float pump2) {
  primeTimes_[0] = pump1 > 0 ? pump1 : 0; primeTimes_[1] = pump2 > 0 ? pump2 : 0; updatePrimeInfo();
}
void MixerUi::refreshChannels() {
  static const char *const titles[] = {"НАСОС 1 / D8", "НАСОС 2 / D10"};
  for(int p = 0; p < 2; ++p) {
    std::string title = titles[p];
    if(!channels_[p].empty()) title += " — " + channels_[p];
    lv_label_set_text(pumpTitles_[p], title.c_str());
    lv_label_set_text(editorPumpLabels_[p], (pumpName(p + 1) + ", мл").c_str());
  }
  const std::string options = pumpName(1) + "\n" + pumpName(2);
  // The narrow calibration selector in the service tab keeps plain pump numbers.
  for(auto *dropdown : {firstPump_, careChannel_}) {
    uint32_t selected = lv_dropdown_get_selected(dropdown);
    lv_dropdown_set_options(dropdown, options.c_str());
    lv_dropdown_set_selected(dropdown, selected);
  }
  lv_textarea_set_text(channelName_, channels_[lv_dropdown_get_selected(careChannel_) ? 1 : 0].c_str());
  updatePrimeInfo();
  updateRecipeDetails();
}
void MixerUi::setChannelNames(const std::string &pump1, const std::string &pump2) {
  channels_[0] = pump1; channels_[1] = pump2; refreshChannels();
}
void MixerUi::setChannelHandler(ChannelHandler h, void *context) { channelHandler_ = h; channelContext_ = context; }
void MixerUi::setChannelMessage(const char *message) { lv_label_set_text(channelMessage_, message); }
void MixerUi::showCare() { dispatch(Action::Care, 0); }
void MixerUi::setEepromInfo(const char *text) { if(eepromInfo_) lv_label_set_text(eepromInfo_, text); }
void MixerUi::showHeightTable(const char *rows) {
  auto *box = lv_msgbox_create(nullptr);
  lv_obj_set_style_text_font(box, &mixer_font_16, 0);
  lv_obj_set_width(box, 440);
  lv_msgbox_add_title(box, "Таблица уровня");
  lv_msgbox_add_text(box, rows);
  auto *content = lv_msgbox_get_content(box);
  lv_obj_set_style_max_height(content, 280, 0);
  lv_obj_set_scroll_dir(content, LV_DIR_VER);
  cancelButton(box, "Понятно");
}
}
