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
