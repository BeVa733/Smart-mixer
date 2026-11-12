#pragma once
#include <stdint.h>
#include <string>
#include <vector>

namespace smart_mixer {
struct Recipe {
  uint32_t id = 0;
  std::string name;
  uint16_t ml1 = 0, ml2 = 0;
  uint8_t firstPump = 1;
  // true: layers, the shaft follows the liquid level; false: mixed drink poured at the rim.
  bool layered = true;
};
inline bool validRecipe(const Recipe &r) {
  if(r.name.empty() || r.name.size() > 64 || r.ml1 > 1000 || r.ml2 > 1000 ||
     (r.ml1 == 0 && r.ml2 == 0) || (r.firstPump != 1 && r.firstPump != 2) ||
     r.name.front() == ' ' || r.name.back() == ' ') return false;
  for(unsigned char c : r.name) if(c < 32 || c == 127) return false;
  return true;
}
enum class RecipeAction { Save, Remove, Prepare };
typedef void (*RecipeHandler)(void *, RecipeAction, const Recipe &);
}
