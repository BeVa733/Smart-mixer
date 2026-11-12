#include "RecipeStore.h"
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <set>
#include <limits>
#include <cerrno>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <cstring>

using smart_mixer::Recipe;
void RecipeStore::load() {
  std::ifstream in(path_);
  if(!in) {
    if(access(path_.c_str(), F_OK) != 0 && errno == ENOENT) { recipes_.clear(); return; }
    throw std::runtime_error("Не удалось открыть файл рецептов");
  }
  std::string line; std::getline(in, line);
  bool legacy = line == "SMART_MIXER_RECIPES 1";
  if(!legacy && line != "SMART_MIXER_RECIPES 2") throw std::runtime_error("Неизвестный формат файла рецептов");
  std::vector<Recipe> loaded; std::set<uint32_t> ids;
  while(std::getline(in, line)) {
    if(loaded.size() >= 32 || line.size() > 120) throw std::runtime_error("Повреждён файл рецептов");
    std::istringstream row(line);
    uint64_t id, ml1, ml2, order; Recipe r;
    if(!(row >> id >> ml1 >> ml2 >> order) || row.get() != ' ' || !std::getline(row, r.name) ||
       id == 0 || id >= std::numeric_limits<uint32_t>::max() || ml1 > 1000 || ml2 > 1000 || order < 1 || order > 2)
      throw std::runtime_error("Повреждена запись рецепта");
    r.id = static_cast<uint32_t>(id); r.ml1 = ml1; r.ml2 = ml2;
    // v1's last number selected a height table, NOT the pump order.
    r.firstPump = legacy ? 1 : order;
    if(!smart_mixer::validRecipe(r) || !ids.insert(r.id).second)
      throw std::runtime_error("Некорректный рецепт или повтор ID");
    loaded.push_back(r);
  }
  if(!in.eof()) throw std::runtime_error("Ошибка чтения рецептов");
  recipes_.swap(loaded);
}
void RecipeStore::commit(const std::vector<Recipe> &next) {
  std::ostringstream out; out << "SMART_MIXER_RECIPES 2\n";
  for(const auto &r : next) out << r.id << ' ' << r.ml1 << ' ' << r.ml2 << ' ' << unsigned(r.firstPump) << ' ' << r.name << '\n';
  auto bytes = out.str();
  std::string temporary = path_ + ".tmp.XXXXXX";
  std::vector<char> name(temporary.begin(), temporary.end()); name.push_back(0);
  int fd = mkstemp(name.data());
  if(fd < 0) throw std::runtime_error("Не удалось создать файл рецептов: " + std::string(strerror(errno)));
  bool ok = true; size_t offset = 0;
  while(offset < bytes.size()) {
    auto n = write(fd, bytes.data() + offset, bytes.size() - offset);
    if(n < 0 && errno == EINTR) continue;
    if(n <= 0) { ok = false; break; }
    offset += static_cast<size_t>(n);
  }
  if(fsync(fd) != 0) ok = false;
  if(close(fd) != 0) ok = false;
  if(ok && rename(name.data(), path_.c_str()) != 0) ok = false;
  if(!ok) { unlink(name.data()); throw std::runtime_error("Рецепты не сохранены; прежний файл оставлен"); }
  auto slash = path_.find_last_of('/');
  std::string parent = slash == std::string::npos ? "." : path_.substr(0, slash);
  int dir = open(parent.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
  if(dir >= 0) { fsync(dir); close(dir); }
  recipes_ = next;
}
void RecipeStore::save(Recipe r) {
  if(!smart_mixer::validRecipe(r)) throw std::runtime_error("Проверьте название и объёмы рецепта");
  auto next = recipes_;
  for(const auto &existing : next) {
    if(existing.id != r.id && existing.name == r.name) throw std::runtime_error("Такое название уже существует");
  }
  for(auto &existing : next) {
    if(r.id && existing.id == r.id) { existing = r; commit(next); return; }
  }
  if(r.id) throw std::runtime_error("Рецепт не найден");
  if(next.size() >= 32) throw std::runtime_error("Можно сохранить до 32 рецептов");
  r.id = 1;
  for(const auto &existing : next) if(existing.id >= r.id) r.id = existing.id + 1;
  if(r.id == UINT32_MAX) throw std::runtime_error("Исчерпаны ID рецептов");
  next.push_back(r); commit(next);
}
void RecipeStore::remove(uint32_t id) {
  auto next = recipes_;
  for(auto i = next.begin(); i != next.end(); ++i) if(i->id == id) { next.erase(i); commit(next); return; }
  throw std::runtime_error("Рецепт не найден");
}
