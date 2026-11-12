#include "RecipeStore.h"
#include <RecipeExecution.h>
#include <fstream>
#include <unistd.h>
#include <cstdio>
#include <stdexcept>
#include <limits>

using namespace smart_mixer;
void check(bool value, const char *message) { if(!value) throw std::runtime_error(message); }
int main() {
  char directory[] = "/tmp/smart-mixer-recipes-XXXXXX";
  check(mkdtemp(directory) != nullptr, "temporary directory");
  std::string path = std::string(directory) + "/recipes.txt";
  RecipeStore store(path); store.load(); check(store.recipes().empty(), "initial catalog empty");
  Recipe a; a.name = "Лимонад с сиропом"; a.ml1 = 30; a.ml2 = 20; a.firstPump = 2;
  store.save(a);
  RecipeStore restarted(path); restarted.load();
  check(restarted.recipes().size() == 1 && restarted.recipes()[0].name == a.name &&
        restarted.recipes()[0].firstPump == 2, "recipes survive process restart including UTF-8");
  a = restarted.recipes()[0]; a.ml1 = 40; restarted.save(a);
  store.load(); check(store.recipes()[0].ml1 == 40, "editing existing recipe");
  Recipe b; b.name = "Другой напиток"; b.ml2 = 10; store.save(b);
  a.name = b.name;
  bool rejected = false;
  try { store.save(a); } catch(const std::exception &) { rejected = true; }
  check(rejected, "duplicate name while editing must not overwrite");
  RecipeStore missing(std::string(directory) + "/missing/recipes.txt");
  rejected = false; try { missing.save(b); } catch(const std::exception &) { rejected = true; }
  check(rejected && missing.recipes().empty(), "write failure must not publish an unsaved recipe");
  store.remove(store.recipes()[0].id); restarted.load(); check(restarted.recipes().size() == 1, "persist deletion");
  { std::ofstream corrupt(path); corrupt << "unknown format\n"; }
  rejected = false; try { restarted.load(); } catch(const std::exception &) { rejected = true; }
  check(rejected && restarted.recipes().size() == 1, "load corruption does not replace existing catalog");
  { std::ofstream legacy(path); legacy << "SMART_MIXER_RECIPES 1\n7 30 20 2 Старый рецепт\n"; }
  restarted.load();
  check(restarted.recipes()[0].firstPump == 1 && restarted.recipes()[0].ml1 == 30 &&
        restarted.recipes()[0].ml2 == 20, "legacy height-table selection must not reverse ingredient order");
  { std::ifstream unchanged(path); std::string header; std::getline(unchanged, header);
    check(header == "SMART_MIXER_RECIPES 1", "loading recipes must not rewrite file"); }
  restarted.save(restarted.recipes()[0]);
  { std::ifstream upgraded(path); std::string header; std::getline(upgraded, header);
    check(header == "SMART_MIXER_RECIPES 2", "explicit save upgrades recipe schema"); }
  unlink(path.c_str()); rmdir(directory);

  HeightSample curve[] = {{5, -100}, {50, -50}, {100, -10}};
  RecipeExecution execution;
  check(execution.start(1, 20, 30, 1000, 1500, curve, 3, 0), "start calibrated recipe");
  int32_t position = 0; bool reachedPump1 = false, reachedPump2 = false;
  for(uint32_t now = 0; now < 5000 && execution.active(); ++now) {
    execution.heartbeat(1, now);
    int direction = execution.step(now, position, position == 0);
    check(position + direction >= -650 && position + direction <= 0, "mechanical limits");
    if(execution.pump() == 1) { reachedPump1 = true; check(position == -10, "first layer must stay at calibrated rim"); }
    if(execution.pump() == 2) {
      check(reachedPump1, "pump order");
      if(!reachedPump2) check(position < -70, "lower to first layer before second pump");
      reachedPump2 = true;
    }
    position += direction;
  }
  check(execution.phase() == DrinkPhase::Done && reachedPump1 && reachedPump2 && position == -10, "return to calibrated rim before DONE");
  RecipeExecution reversed;
  check(reversed.start(9, 30, 20, 900, 1600, curve, 3, 0, 2), "pump 2 may pour first");
  position = 0; uint32_t firstAt = 0, secondAt = 0; bool firstSeen = false, secondSeen = false;
  for(uint32_t now = 0; now < 5000 && reversed.active(); ++now) {
    auto previous = reversed.phase(); reversed.heartbeat(9, now);
    int direction = reversed.step(now, position, position == 0);
    if(reversed.phase() == DrinkPhase::FirstLayer) {
      check(reversed.pump() == 2 && position == -10, "first physical pump is 2, at rim");
      if(!firstSeen) { firstSeen = true; firstAt = now; }
    }
    if(previous == DrinkPhase::FirstLayer && reversed.phase() == DrinkPhase::Lowering)
      check(now - firstAt == 1600, "first duration must belong to pump 2");
    if(reversed.phase() == DrinkPhase::SecondLayer) {
      check(reversed.pump() == 1, "second physical pump is 1");
      if(!secondSeen) {
        secondSeen = true; secondAt = now;
        check(position == -84, "lowering must use pump 2's first-layer 20 ml in common curve");
      }
    }
    if(previous == DrinkPhase::SecondLayer && reversed.phase() == DrinkPhase::Returning)
      check(now - secondAt == 900, "second duration must belong to pump 1");
    position += direction;
  }
  check(firstSeen && secondSeen && reversed.phase() == DrinkPhase::Done && position == -10, "reverse order completes at common calibrated rim");
  RecipeExecution validation;
  check(!validation.start(20, 1, 20, 100, 1000, curve, 3, 0, 1) &&
        validation.start(20, 1, 20, 100, 1000, curve, 3, 0, 2), "minimum first-layer volume follows selected pump");
  check(!execution.start(1, 20, 30, 1000, 1500, curve, 3, 6000), "duplicate latest operation rejected");
  check(!execution.start(2, 100, 100, 1000, 1000, curve, 3, 6000), "volume beyond height table rejected");
  HeightSample invalid[] = {{5, -100}, {10, -200}};
  check(!execution.start(2, 5, 5, 500, 500, invalid, 2, 6000), "inverted height curve rejected");
  check(execution.start(2, 0, 10, 0, 500, curve, 3, 0xFFFFFF00U), "pump 2 only and clock rollover");
  execution.step(0xFFFFFF01U, 0, true);
  execution.step(static_cast<uint32_t>(0xFFFFFF00U + 2000U), -1, false);
  check(execution.phase() == DrinkPhase::Error && std::string(execution.error()) == "LINK_TIMEOUT" && execution.pump() == 0, "link timeout stops all phases across rollover");
  check(execution.start(3, 10, 0, 500, 0, curve, 3, 0), "restart only explicit new operation");
  execution.cancel(); check(!execution.active() && execution.pump() == 0, "cancel without automatic return");
  for(unsigned first : {1U, 2U}) for(auto phase : {DrinkPhase::Positioning, DrinkPhase::FirstLayer, DrinkPhase::Lowering, DrinkPhase::SecondLayer, DrinkPhase::Returning}) {
    RecipeExecution tested; check(tested.start(10, 20, 30, 1000, 1500, curve, 3, 0, first), "phase test start");
    int32_t p = 0; uint32_t now = 0;
    while(tested.phase() != phase && tested.active() && now < 5000) {
      tested.heartbeat(10, now); p += tested.step(now, p, p == 0); ++now;
    }
    check(tested.phase() == phase, "target phase reached");
    tested.step(now + 2000, p, p == 0);
    check(tested.phase() == DrinkPhase::Error && tested.pump() == 0, "watchdog stops every phase");
  }
  uint32_t values[4]; const uint32_t limits[] = {UINT32_MAX, 1000, 1000, 2};
  check(parseRecipeValues("4294967295 20 30 2", values, limits, 4), "largest operation id");
  for(const char *bad : {"4294967296 20 30 1", "1 -5 30 1", "1 20 30 3", "1 20 30 1 extra", "1 20", "1 1001 0 1"})
    check(!parseRecipeValues(bad, values, limits, 4), "strict recipe command parser");
  puts("Recipe storage, atomic publication, validation, following curve, pump order, completion, cancel and watchdog passed.");
}
