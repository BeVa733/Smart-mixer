#include "RecipeStore.h"
#include <RecipeExecution.h>
#include <fstream>
#include <unistd.h>
#include <cstdio>
#include <stdexcept>
#include <limits>
#include <cmath>

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
    check(header == "SMART_MIXER_RECIPES 3", "explicit save upgrades recipe schema"); }
  check(restarted.recipes()[0].layered, "legacy recipes stay layered");
  { std::ofstream v2(path); v2 << "SMART_MIXER_RECIPES 2\n4 10 20 2 Слои v2\n"; }
  restarted.load();
  check(restarted.recipes()[0].layered && restarted.recipes()[0].firstPump == 2, "v2 recipe is layered with its pump order");
  Recipe mixed = restarted.recipes()[0]; mixed.layered = false; restarted.save(mixed);
  RecipeStore reread(path); reread.load();
  check(!reread.recipes()[0].layered && reread.recipes()[0].name == "Слои v2", "mixed mode survives restart");
  { std::ofstream bad(path); bad << "SMART_MIXER_RECIPES 3\n4 10 20 2 7 Плохой\n"; }
  rejected = false; try { reread.load(); } catch(const std::exception &) { rejected = true; }
  check(rejected, "invalid recipe mode rejected");
  ChannelStore channels(std::string(directory) + "/channels.txt");
  channels.load(); check(channels.name(1).empty() && channels.name(2).empty(), "no channel names initially");
  channels.save(1, "Гренадин"); channels.save(2, "Апельсиновый сок");
  ChannelStore channelsRestarted(std::string(directory) + "/channels.txt"); channelsRestarted.load();
  check(channelsRestarted.name(1) == "Гренадин" && channelsRestarted.name(2) == "Апельсиновый сок", "channel names survive restart");
  channelsRestarted.save(1, ""); channels.load();
  check(channels.name(1).empty() && channels.name(2) == "Апельсиновый сок", "channel name removal");
  rejected = false; try { channels.save(2, "bad\tname"); } catch(const std::exception &) { rejected = true; }
  check(rejected && channels.name(2) == "Апельсиновый сок", "control characters rejected without change");
  { std::ofstream corrupt(std::string(directory) + "/channels.txt"); corrupt << "SMART_MIXER_CHANNELS 1\n3 x\n"; }
  rejected = false; try { channels.load(); } catch(const std::exception &) { rejected = true; }
  check(rejected && channels.name(2) == "Апельсиновый сок", "damaged channel file keeps names in memory");
  { std::ofstream v1(std::string(directory) + "/channels.txt"); v1 << "SMART_MIXER_CHANNELS 1\n1 Гренадин\n"; }
  channels.load();
  check(channels.name(1) == "Гренадин" && channels.primeTime(1) == 0 && channels.name(2).empty(), "v1 channel file without line times");
  channels.savePrimeTime(2, 12.34f);
  ChannelStore timed(std::string(directory) + "/channels.txt"); timed.load();
  check(timed.name(1) == "Гренадин" && std::fabs(timed.primeTime(2) - 12.34f) < 0.001f && timed.primeTime(1) == 0,
        "line time survives restart and keeps names");
  timed.save(2, "Сок"); channels.load();
  check(channels.name(2) == "Сок" && std::fabs(channels.primeTime(2) - 12.34f) < 0.001f, "renaming keeps line time");
  rejected = false; try { channels.savePrimeTime(1, 301); } catch(const std::exception &) { rejected = true; }
  check(rejected && channels.primeTime(1) == 0, "line time limit");
  { std::ofstream bad(std::string(directory) + "/channels.txt"); bad << "SMART_MIXER_CHANNELS 2\n1 abc Имя\n"; }
  rejected = false; try { channels.load(); } catch(const std::exception &) { rejected = true; }
  check(rejected, "damaged line time rejected");
  unlink((std::string(directory) + "/channels.txt").c_str());
  unlink(path.c_str()); rmdir(directory);

  HeightSample curve[] = {{5, -100}, {50, -50}, {100, -10}};
  RecipeExecution execution;
  check(execution.start(1, 20, 30, 1000, 1500, curve, 3, 0), "start calibrated recipe");
  int32_t position = 0; bool reachedPump1 = false, reachedPump2 = false;
  for(uint32_t now = 0; now < 5000 && execution.active(); ++now) {
    execution.heartbeat(1, now);
    int direction = execution.step(now, position, position == 0);
    check(position + direction >= -1000 && position + direction <= 0, "mechanical limits");
    if(execution.pump() == 1) { reachedPump1 = true; check(position == -10, "first layer must stay at calibrated rim"); }
    if(execution.pump() == 2) {
      check(reachedPump1, "pump order");
      if(!reachedPump2) check(position == -84 + RecipeExecution::LEVEL_LEAD_STEPS, "lower to just above the first layer");
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
        check(position == -84 + RecipeExecution::LEVEL_LEAD_STEPS, "lowering must use pump 2's first-layer 20 ml in common curve");
      }
    }
    if(previous == DrinkPhase::SecondLayer && reversed.phase() == DrinkPhase::Returning)
      check(now - secondAt == 900, "second duration must belong to pump 1");
    position += direction;
  }
  check(firstSeen && secondSeen && reversed.phase() == DrinkPhase::Done && position == -10, "reverse order completes at common calibrated rim");
  // Lead over the liquid during the second layer: 0..30 microsteps above the calculated level.
  {
    auto level = [&](double ml) {
      for(int i = 1; i < 3; ++i) if(ml <= curve[i].ml)
        return curve[i-1].steps + (curve[i].steps - curve[i-1].steps) * (ml - curve[i-1].ml) / (curve[i].ml - curve[i-1].ml);
      return static_cast<double>(curve[2].steps);
    };
    RecipeExecution lead; check(lead.start(50, 20, 30, 1000, 1500, curve, 3, 0), "lead test start");
    int32_t p = 0; uint32_t secondAt = 0; bool second = false; double minLead = 1e9, maxLead = -1e9;
    for(uint32_t now = 0; now < 6000 && lead.active(); ++now) {
      lead.heartbeat(50, now); p += lead.step(now, p, p == 0);
      if(lead.phase() == DrinkPhase::Lowering || lead.phase() == DrinkPhase::SecondLayer)
        check(p >= -84, "shaft never goes below the liquid while following layers");
      if(lead.phase() == DrinkPhase::SecondLayer) {
        if(!second) { second = true; secondAt = now; }
        double gap = p - level(20 + 30.0 * (now - secondAt) / 1500);
        if(gap < minLead) minLead = gap;
        if(gap > maxLead) maxLead = gap;
      }
    }
    check(second && lead.phase() == DrinkPhase::Done, "lead recipe completes");
    check(minLead >= 0 && maxLead <= 30, "shaft leads the second layer by 0..30 microsteps");
  }
  RecipeExecution validation;
  check(!validation.start(20, 1, 20, 100, 1000, curve, 3, 0, 1) &&
        validation.start(20, 1, 20, 100, 1000, curve, 3, 0, 2), "minimum first-layer volume follows selected pump");
  check(!execution.start(1, 20, 30, 1000, 1500, curve, 3, 6000), "duplicate latest operation rejected");
  check(!execution.start(2, 100, 100, 1000, 1000, curve, 3, 6000), "volume beyond height table rejected");
  HeightSample deep[] = {{5, -1000}, {100, -800}};
  RecipeExecution deepRun; check(deepRun.start(40, 20, 30, 500, 500, deep, 2, 0), "curve down to 1000 microsteps accepted");
  { int32_t p = 0; for(uint32_t now = 0; now < 20000 && deepRun.active(); ++now) {
      deepRun.heartbeat(40, now); int d = deepRun.step(now, p, p == 0); p += d;
      check(p >= -1000, "recipe never moves below the limit"); }
    check(deepRun.phase() == DrinkPhase::Done && p == -800, "deep recipe completes at its rim"); }
  HeightSample tooDeep[] = {{5, -1001}, {100, -800}};
  RecipeExecution tooDeepRun; check(!tooDeepRun.start(41, 20, 30, 500, 500, tooDeep, 2, 0), "curve below the limit rejected");
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
  // Mixed mode: everything is poured at the rim, the shaft never lowers.
  RecipeExecution mixedRun;
  check(mixedRun.start(30, 2, 30, 300, 1500, curve, 3, 0, 1, false), "mixed mode allows a first portion below the curve");
  position = 0; bool lowered = false, mixedPump1 = false, mixedPump2 = false;
  for(uint32_t now = 0; now < 5000 && mixedRun.active(); ++now) {
    mixedRun.heartbeat(30, now);
    int direction = mixedRun.step(now, position, position == 0);
    if(mixedRun.phase() == DrinkPhase::Lowering) lowered = true;
    if(mixedRun.pump()) check(position == -10, "mixed mode pours only at the rim");
    if(mixedRun.pump() == 1) mixedPump1 = true;
    if(mixedRun.pump() == 2) { check(mixedPump1, "mixed mode keeps pump order"); mixedPump2 = true; }
    position += direction;
  }
  check(!lowered && mixedPump1 && mixedPump2 && mixedRun.phase() == DrinkPhase::Done && position == -10, "mixed recipe completes at rim");
  RecipeExecution mixedCapacity;
  check(!mixedCapacity.start(31, 60, 60, 1000, 1000, curve, 3, 0, 1, false), "mixed mode still respects glass capacity");
  uint32_t mode[5]; const uint32_t modeLimits[] = {UINT32_MAX, 1000, 1000, 2, 1};
  check(parseRecipeValues("7 20 30 1 0", mode, modeLimits, 5) && mode[4] == 0, "API 3 mix command with mode");
  check(!parseRecipeValues("7 20 30 1 2", mode, modeLimits, 5) && !parseRecipeValues("7 20 30 1", mode, modeLimits, 5),
        "mode must be 0 or 1; API 2 form is parsed separately");
  uint32_t values[4]; const uint32_t limits[] = {UINT32_MAX, 1000, 1000, 2};
  check(parseRecipeValues("4294967295 20 30 2", values, limits, 4), "largest operation id");
  for(const char *bad : {"4294967296 20 30 1", "1 -5 30 1", "1 20 30 3", "1 20 30 1 extra", "1 20", "1 1001 0 1"})
    check(!parseRecipeValues(bad, values, limits, 4), "strict recipe command parser");
  puts("Recipe storage v1-v3, channel names, layered and mixed modes, pump order, completion, cancel and watchdog passed.");
}
