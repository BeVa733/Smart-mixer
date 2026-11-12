#pragma once
#include <lvgl.h>
#include <stdint.h>
#include "NumericInput.h"
#include "Recipe.h"

namespace smart_mixer {
// Logical actions only. Hardware adapters own validation, execution and replies.
enum class Action { Connect, Disconnect, Status, Stop, Home, Encoder, Move,
                    Dose, PumpOn, PumpOff, Prime, Measure, CheckDose, SaveRate,
                    HeightStart, HeightMark, HeightStop, HeightShow, HeightClear,
                    MainMenu, Service, Recipes, NewRecipe, EditRecipe, SaveRecipe,
                    RemoveRecipe, PrepareRecipe };
struct Command { Action action; int pump; float value; };
typedef void (*CommandHandler)(void *, const Command &);

class MixerUi {
public:
  void create(CommandHandler handler, void *context);
  void setConnection(bool connected, bool working, const char *message);
  void setStatus(const char *text);
  void setLog(const char *text);
  void setPort(const char *text);
  const char *port() const;
  void showTab(unsigned index);
  void request(const Command &command);
  void showMain();
  void showRecipes();
  void showNewRecipe() { dispatch(Action::NewRecipe, 0); }
  lv_obj_t *recipeEditorRoot() const { return editor_; }
  void setRecipes(const std::vector<Recipe> &recipes);
  void setRecipeHandler(RecipeHandler handler, void *context);
  void setRecipeMessage(const char *message);
  void setCooking(bool ready, bool busy, const char *message, int percent);
  void setCalibrating(bool active);
  lv_obj_t *serviceRoot() const { return tabs_; }
private:
  struct Binding { MixerUi *ui; Action action; int pump; };
  Binding bindings_[64]{};
  unsigned count_ = 0;
  CommandHandler handler_ = nullptr;
  void *context_ = nullptr;
  lv_obj_t *tabs_ = nullptr, *connection_ = nullptr, *status_ = nullptr;
  lv_obj_t *back_ = nullptr, *statusButton_ = nullptr, *stopButton_ = nullptr;
  lv_obj_t *visiblePage_ = nullptr;
  bool calibrating_ = false;
  lv_obj_t *log_ = nullptr, *port_ = nullptr, *pump_ = nullptr;
  NumericInput volume_[2], steps_, grams_;
  lv_obj_t *controls_[40]{};
  unsigned controlCount_ = 0;
  bool working_ = false;
  lv_obj_t *main_ = nullptr, *catalog_ = nullptr, *editor_ = nullptr;
  lv_obj_t *recipeList_ = nullptr, *recipeDetails_ = nullptr, *recipeMessage_ = nullptr;
  lv_obj_t *recipeName_ = nullptr, *editorMessage_ = nullptr, *progress_ = nullptr;
  lv_obj_t *firstPump_ = nullptr;
  lv_obj_t *recipeButtons_[3]{};
  NumericInput recipeMl1_, recipeMl2_;
  std::vector<Recipe> recipes_;
  uint32_t editingId_ = 0;
  bool recipeReady_ = false, cooking_ = false;
  Recipe pendingRecipe_;
  RecipeAction pendingRecipeAction_ = RecipeAction::Prepare;
  RecipeHandler recipeHandler_ = nullptr;
  void *recipeContext_ = nullptr;
  Command pending_{Action::Status, 0, 0};
  lv_obj_t *button(lv_obj_t *, const char *, int, int, int, Action, int = 0, bool = true);
  static void clicked(lv_event_t *);
  static void confirmed(lv_event_t *);
  void dispatch(Action action, int pump);
  void showPage(lv_obj_t *page);
  void updateNavigation();
  void showNotice(const char *message);
  void createRecipePages(lv_obj_t *screen);
  void updateRecipeDetails();
  const Recipe *selectedRecipe() const;
  static void recipeSelected(lv_event_t *);
  static void recipeConfirmed(lv_event_t *);
};
}
