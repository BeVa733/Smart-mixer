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
  auto *keyboard = lv_sdl_keyboard_create(); lv_indev_set_group(keyboard, group);
  UsbController usb;
  smart_mixer::MixerUi ui;
  if(recipePath.empty()) recipePath = smoke ? "/tmp/smart-mixer-ui-smoke-recipes.txt" : defaultRecipes();
  RecipeStore recipes(recipePath);
  std::string storageError;
  struct Context { UsbController *usb; smart_mixer::MixerUi *ui; RecipeStore *store; bool writable; } context{&usb, &ui, &recipes, false};
  ui.create([](void *p, const smart_mixer::Command &c) {
    auto *ctx = static_cast<Context *>(p);
    ctx->usb->handle(c, ctx->ui->port());
  }, &context);
  try {
    if(!smoke) { auto slash = recipePath.find_last_of('/'); if(slash != std::string::npos) directories(recipePath.substr(0, slash)); }
    recipes.load(); context.writable = true; ui.setRecipes(recipes.recipes());
  } catch(const std::exception &e) { storageError = e.what(); ui.setRecipeMessage(e.what()); }
  ui.setRecipeHandler([](void *p, smart_mixer::RecipeAction action, const smart_mixer::Recipe &recipe) {
    auto *ctx = static_cast<Context *>(p);
    try {
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
    if(smoke) {
      if(frames % 10 == 9 && frames < 70) {
        auto *image = SDL_CreateRGBSurfaceWithFormat(0, 800, 480, 32, SDL_PIXELFORMAT_ARGB8888);
        auto *renderer = static_cast<SDL_Renderer *>(lv_sdl_window_get_renderer(display));
        if(image && SDL_RenderReadPixels(renderer, nullptr, SDL_PIXELFORMAT_ARGB8888,
                                         image->pixels, image->pitch) == 0) {
          char path[100];
          if(frames < 30) snprintf(path, sizeof(path), "/tmp/smart-mixer-ui-page-%u.bmp", frames / 10);
          else snprintf(path, sizeof(path), "/tmp/smart-mixer-ui-tab-%u.bmp", frames / 10 - 3);
          SDL_SaveBMP(image, path);
        }
        if(image) SDL_FreeSurface(image);
      }
      if(frames == 0) ui.showMain();
      if(frames == 10) ui.showRecipes();
      if(frames == 20) ui.showNewRecipe();
      if(frames >= 30 && frames % 10 == 0) ui.showTab(frames / 10 - 3);
      if(++frames == 75) break;
    }
    SDL_Delay(5);
  }
  if(smoke) {
    // No hardware is opened in smoke mode. Verify the USB action mapping too.
    using namespace smart_mixer;
    if(UsbController::encode({Action::Dose, 2, 50}) != "d2 50.00" ||
       UsbController::encode({Action::Move, 0, -10}) != "a -10") return 2;
    try { UsbController::encode({Action::Move, 0, 651}); return 3; }
    catch(const std::exception &) {}
    puts("LVGL: all four tabs rendered; USB action mapping checked; no hardware accessed.");
  }
  lv_sdl_quit();
  lv_deinit();
}
