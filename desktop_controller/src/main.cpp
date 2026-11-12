#include "UsbController.h"
#include <SDL2/SDL.h>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include "RecipeStore.h"
#include <cstdlib>
#include <sys/stat.h>
#include <cerrno>

namespace {
void directories(const std::string &path) {
  for(size_t i = 1; i <= path.size(); ++i) if(i == path.size() || path[i] == '/') {
    std::string part = path.substr(0, i);
    if(!part.empty() && mkdir(part.c_str(), 0700) != 0 && errno != EEXIST)
      throw std::runtime_error("Не удалось создать каталог рецептов");
  }
}
// LVGL's SDL wheel only scrolls the last clicked object. Scroll the panel under the
// mouse pointer instead, like a desktop application. Events are collected by an SDL
// watch and applied from the main loop, outside LVGL's event processing.
int wheelSteps = 0, wheelX = 0, wheelY = 0;
int watchWheel(void *, SDL_Event *event) {
  if(event->type == SDL_MOUSEWHEEL) {
    wheelSteps += event->wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -event->wheel.y : event->wheel.y;
    SDL_GetMouseState(&wheelX, &wheelY);
  }
  return 0;
}
void scrollUnderPointer() {
  if(!wheelSteps) return;
  lv_point_t point{wheelX, wheelY};
  lv_obj_t *target = lv_obj_get_child_count(lv_layer_top()) ? lv_indev_search_obj(lv_layer_top(), &point) : nullptr;
  if(!target) target = lv_indev_search_obj(lv_screen_active(), &point);
  for(lv_obj_t *obj = target; obj; obj = lv_obj_get_parent(obj)) {
    if(lv_obj_has_flag(obj, LV_OBJ_FLAG_SCROLLABLE) &&
       (lv_obj_get_scroll_top(obj) > 0 || lv_obj_get_scroll_bottom(obj) > 0)) {
      lv_obj_scroll_by_bounded(obj, 0, wheelSteps * 40, LV_ANIM_OFF);
      break;
    }
  }
  wheelSteps = 0;
}
std::string defaultRecipes() {
  const char *xdg = getenv("XDG_DATA_HOME"), *home = getenv("HOME");
  if(xdg && xdg[0] == '/') return std::string(xdg) + "/smart-mixer/recipes.txt";
  if(home) return std::string(home) + "/.local/share/smart-mixer/recipes.txt";
  throw std::runtime_error("Не задан каталог пользователя для рецептов");
}
}

int main(int argc, char **argv) {
  bool smoke = false; std::string recipePath, device;
  for(int i = 1; i < argc; ++i) {
    if(strcmp(argv[i], "--smoke-test") == 0) smoke = true;
    else if(strcmp(argv[i], "--recipes") == 0 && i + 1 < argc) recipePath = argv[++i];
    else if(strcmp(argv[i], "--device") == 0 && i + 1 < argc) device = argv[++i];
    else { fprintf(stderr, "Usage: smart_mixer [--device PATH] [--recipes PATH] [--smoke-test]\n"); return 1; }
  }
  lv_init();
  auto *display = lv_sdl_window_create(800, 480);
  lv_sdl_window_set_title(display, "Smart Mixer / LVGL / USB");
  lv_sdl_window_set_resizeable(display, false);
  auto *group = lv_group_create(); lv_group_set_default(group);
  lv_sdl_mouse_create();
  SDL_AddEventWatch(watchWheel, nullptr);
  auto *keyboard = lv_sdl_keyboard_create(); lv_indev_set_group(keyboard, group);
  UsbController usb;
  smart_mixer::MixerUi ui;
  if(recipePath.empty()) recipePath = smoke ? "/tmp/smart-mixer-ui-smoke-recipes.txt" : defaultRecipes();
  RecipeStore recipes(recipePath);
  // Ingredient names live next to the recipes file.
  auto recipeSlash = recipePath.find_last_of('/');
  ChannelStore channels((recipeSlash == std::string::npos ? std::string(".") : recipePath.substr(0, recipeSlash)) +
                        (smoke ? "/smart-mixer-ui-smoke-channels.txt" : "/channels.txt"));
  std::string storageError;
  struct Context { UsbController *usb; smart_mixer::MixerUi *ui; RecipeStore *store; ChannelStore *channels;
                   bool writable, channelsWritable; } context{&usb, &ui, &recipes, &channels, false, false};
  ui.create([](void *p, const smart_mixer::Command &c) {
    auto *ctx = static_cast<Context *>(p);
    ctx->usb->handle(c, ctx->ui->port());
  }, &context);
  try {
    if(!smoke) { auto slash = recipePath.find_last_of('/'); if(slash != std::string::npos) directories(recipePath.substr(0, slash)); }
    recipes.load(); context.writable = true; ui.setRecipes(recipes.recipes());
  } catch(const std::exception &e) { storageError = e.what(); ui.setRecipeMessage(e.what()); }
  try {
    channels.load(); context.channelsWritable = true;
    ui.setChannelNames(channels.name(1), channels.name(2));
    ui.setPrimeTimes(channels.primeTime(1), channels.primeTime(2));
  } catch(const std::exception &e) { ui.setChannelMessage(e.what()); }
  ui.setChannelHandler([](void *p, int pump, const std::string &name) {
    auto *ctx = static_cast<Context *>(p);
    try {
      if(!ctx->channelsWritable) throw std::runtime_error("Файл каналов недоступен. Исправьте его и перезапустите программу");
      ctx->channels->save(pump, name);
      ctx->ui->setChannelNames(ctx->channels->name(1), ctx->channels->name(2));
      ctx->ui->setChannelMessage(name.empty() ? "Название удалено" : "Название сохранено");
    } catch(const std::exception &e) { ctx->ui->setChannelMessage(e.what()); }
  }, &context);
  ui.setRecipeHandler([](void *p, smart_mixer::RecipeAction action, const smart_mixer::Recipe &recipe) {
    auto *ctx = static_cast<Context *>(p);
    try {
      if(action == smart_mixer::RecipeAction::Check) {
        std::string problem = ctx->usb->cookingProblem(recipe);
        if(!problem.empty()) ctx->ui->rejectRecipe(problem.c_str());
        return;
      }
      if(action == smart_mixer::RecipeAction::Prepare) { ctx->usb->prepare(recipe); return; }
      if(!ctx->writable) throw std::runtime_error("Хранилище недоступно. Исправьте файл рецептов и перезапустите программу");
      if(action == smart_mixer::RecipeAction::Save) ctx->store->save(recipe);
      else ctx->store->remove(recipe.id);
      ctx->ui->setRecipes(ctx->store->recipes());
      ctx->ui->showRecipes();
      ctx->ui->setRecipeMessage(action == smart_mixer::RecipeAction::Save ? "Рецепт сохранён" : "Рецепт удалён");
    } catch(const std::exception &e) { ctx->ui->setRecipeMessage(e.what()); }
  }, &context);
  if(!device.empty()) ui.setPort(device.c_str());
  unsigned frames = 0;
  std::string previousMessage, previousStatus, previousLog;
  std::string previousCooking;
  int previousPercent = -1; bool previousReady = false, previousBusy = false;
  bool previousCalibration = false;
  while(lv_display_get_next(nullptr)) {
    usb.poll();
    {
      static std::string previousEeprom;
      std::string eeprom = usb.eepromSummary();
      if(eeprom != previousEeprom) { previousEeprom = eeprom; ui.setEepromInfo(eeprom.c_str()); }
    }
    std::string heightTable;
    if(usb.takeHeightTable(heightTable)) ui.showHeightTable(heightTable.c_str());
    int primedPump; float primeSeconds;
    if(usb.takePrimeMeasurement(primedPump, primeSeconds)) {
      char text[120];
      try {
        if(!context.channelsWritable) throw std::runtime_error("Файл каналов недоступен; время трассы не сохранено");
        channels.savePrimeTime(primedPump, primeSeconds);
        ui.setPrimeTimes(channels.primeTime(1), channels.primeTime(2));
        snprintf(text, sizeof(text), "Насос %d: время трассы %.1f с сохранено", primedPump, primeSeconds);
        ui.setChannelMessage(text);
      } catch(const std::exception &e) { ui.setChannelMessage(e.what()); }
    }
    if(usb.calibrating() != previousCalibration) {
      previousCalibration = usb.calibrating(); ui.setCalibrating(previousCalibration);
    }
    if(usb.message() != previousMessage) {
      previousMessage = usb.message(); ui.setConnection(usb.connected(), usb.working(), previousMessage.c_str());
    }
    if(usb.status() != previousStatus) { previousStatus = usb.status(); ui.setStatus(previousStatus.c_str()); }
    if(usb.log() != previousLog) { previousLog = usb.log(); ui.setLog(previousLog.c_str()); }
    if(usb.cookingMessage() != previousCooking || usb.progress() != previousPercent ||
       usb.recipeReady() != previousReady || usb.cooking() != previousBusy) {
      previousCooking = usb.cookingMessage(); previousPercent = usb.progress();
      previousReady = usb.recipeReady(); previousBusy = usb.cooking();
      ui.setCooking(previousReady, previousBusy, previousCooking.c_str(), previousPercent);
      if(!storageError.empty()) ui.setRecipeMessage(storageError.c_str());
    }
    lv_timer_handler();
    scrollUnderPointer();
    if(smoke) {
      if(frames % 10 == 9 && frames < 80) {
        auto *image = SDL_CreateRGBSurfaceWithFormat(0, 800, 480, 32, SDL_PIXELFORMAT_ARGB8888);
        auto *renderer = static_cast<SDL_Renderer *>(lv_sdl_window_get_renderer(display));
        if(image && SDL_RenderReadPixels(renderer, nullptr, SDL_PIXELFORMAT_ARGB8888,
                                         image->pixels, image->pitch) == 0) {
          char path[100];
          if(frames < 40) snprintf(path, sizeof(path), "/tmp/smart-mixer-ui-page-%u.bmp", frames / 10);
          else snprintf(path, sizeof(path), "/tmp/smart-mixer-ui-tab-%u.bmp", frames / 10 - 4);
          SDL_SaveBMP(image, path);
        }
        if(image) SDL_FreeSurface(image);
      }
      if(frames == 0) ui.showMain();
      if(frames == 10) ui.showRecipes();
      if(frames == 20) ui.showNewRecipe();
      if(frames == 30) ui.showCare();
      if(frames >= 40 && frames % 10 == 0) ui.showTab(frames / 10 - 4);
      if(++frames == 85) break;
    }
    SDL_Delay(5);
  }
  if(smoke) {
    // No hardware is opened in smoke mode. Verify the USB action mapping too.
    using namespace smart_mixer;
    if(UsbController::encode({Action::Dose, 2, 50}) != "d2 50.00" ||
       UsbController::encode({Action::Move, 0, -10}) != "a -10") return 2;
    if(UsbController::encode({Action::Move, 0, -1000}) != "a -1000") return 4;
    try { UsbController::encode({Action::Move, 0, 1001}); return 3; }
    catch(const std::exception &) {}
    puts("LVGL: main, recipes, editor, care page and four tabs rendered; USB action mapping checked; no hardware accessed.");
  }
  lv_sdl_quit();
  lv_deinit();
}
