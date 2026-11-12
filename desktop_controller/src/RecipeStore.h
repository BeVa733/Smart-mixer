#pragma once
#include <Recipe.h>
#include <string>

class RecipeStore {
public:
  explicit RecipeStore(const std::string &path) : path_(path) {}
  void load();
  void save(smart_mixer::Recipe recipe);
  void remove(uint32_t id);
  const std::vector<smart_mixer::Recipe> &recipes() const { return recipes_; }
  const std::string &path() const { return path_; }
private:
  std::string path_;
  std::vector<smart_mixer::Recipe> recipes_;
  void commit(const std::vector<smart_mixer::Recipe> &);
};

// Ingredient names and calibrated line-fill times of physical pumps 1 and 2. Stored on the PC next to recipes,
// like recipes: atomic replace, a damaged file is never overwritten automatically.
class ChannelStore {
public:
  explicit ChannelStore(const std::string &path) : path_(path) {}
  void load();
  void save(int pump, const std::string &name);
  void savePrimeTime(int pump, float seconds);
  const std::string &name(int pump) const { return names_[pump == 2 ? 1 : 0]; }
  // Seconds to fill the line from an empty tube; 0 = not calibrated.
  float primeTime(int pump) const { return primeTimes_[pump == 2 ? 1 : 0]; }
private:
  std::string path_, names_[2];
  float primeTimes_[2]{};
  void commit(const std::string names[2], const float times[2]);
};

// Write to a temporary file, fsync, then rename over the target.
void writeFileAtomically(const std::string &path, const std::string &bytes);
