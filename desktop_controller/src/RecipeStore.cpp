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
#include <cstdio>
#include <cstdlib>

using smart_mixer::Recipe;
void RecipeStore::load() {
  std::ifstream in(path_);
  if(!in) {
    if(access(path_.c_str(), F_OK) != 0 && errno == ENOENT) { recipes_.clear(); return; }
    throw std::runtime_error("Не удалось открыть файл рецептов");
  }
  std::string line; std::getline(in, line);
  bool legacy = line == "SMART_MIXER_RECIPES 1";
  bool withMode = line == "SMART_MIXER_RECIPES 3";
  if(!legacy && !withMode && line != "SMART_MIXER_RECIPES 2") throw std::runtime_error("Неизвестный формат файла рецептов");
  std::vector<Recipe> loaded; std::set<uint32_t> ids;
  while(std::getline(in, line)) {
    if(loaded.size() >= 32 || line.size() > 120) throw std::runtime_error("Повреждён файл рецептов");
    std::istringstream row(line);
    uint64_t id, ml1, ml2, order, layered = 1; Recipe r;
    if(!(row >> id >> ml1 >> ml2 >> order) || (withMode && !(row >> layered)) ||
       row.get() != ' ' || !std::getline(row, r.name) ||
       id == 0 || id >= std::numeric_limits<uint32_t>::max() || ml1 > 1000 || ml2 > 1000 || order < 1 || order > 2 ||
       layered > 1)
      throw std::runtime_error("Повреждена запись рецепта");
    // v1/v2 recipes were always layered.
    r.layered = layered == 1;
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
void writeFileAtomically(const std::string &path, const std::string &bytes) {
  std::string temporary = path + ".tmp.XXXXXX";
  std::vector<char> name(temporary.begin(), temporary.end()); name.push_back(0);
  int fd = mkstemp(name.data());
  if(fd < 0) throw std::runtime_error("Не удалось создать файл: " + std::string(strerror(errno)));
  bool ok = true; size_t offset = 0;
  while(offset < bytes.size()) {
    auto n = write(fd, bytes.data() + offset, bytes.size() - offset);
    if(n < 0 && errno == EINTR) continue;
    if(n <= 0) { ok = false; break; }
    offset += static_cast<size_t>(n);
  }
  if(fsync(fd) != 0) ok = false;
  if(close(fd) != 0) ok = false;
  if(ok && rename(name.data(), path.c_str()) != 0) ok = false;
  if(!ok) { unlink(name.data()); throw std::runtime_error("Не сохранено; прежний файл оставлен"); }
  auto slash = path.find_last_of('/');
  std::string parent = slash == std::string::npos ? "." : path.substr(0, slash);
  int dir = open(parent.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
  if(dir >= 0) { fsync(dir); close(dir); }
}
void RecipeStore::commit(const std::vector<Recipe> &next) {
  std::ostringstream out; out << "SMART_MIXER_RECIPES 3\n";
  for(const auto &r : next) out << r.id << ' ' << r.ml1 << ' ' << r.ml2 << ' ' << unsigned(r.firstPump) << ' '
                               << (r.layered ? 1 : 0) << ' ' << r.name << '\n';
  try { writeFileAtomically(path_, out.str()); }
  catch(const std::exception &) { throw std::runtime_error("Рецепты не сохранены; прежний файл оставлен"); }
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

void ChannelStore::load() {
  std::ifstream in(path_);
  if(!in) {
    if(access(path_.c_str(), F_OK) != 0 && errno == ENOENT) {
      names_[0].clear(); names_[1].clear(); primeTimes_[0] = primeTimes_[1] = 0; return;
    }
    throw std::runtime_error("Не удалось открыть файл каналов");
  }
  std::string line, loaded[2]; float times[2] = {0, 0}; std::getline(in, line);
  // v1: "N name"; v2: "N seconds name".
  bool withTime = line == "SMART_MIXER_CHANNELS 2";
  if(!withTime && line != "SMART_MIXER_CHANNELS 1") throw std::runtime_error("Неизвестный формат файла каналов");
  bool seen[2] = {false, false};
  while(std::getline(in, line)) {
    if(line.size() < 2 || (line[0] != '1' && line[0] != '2') || line[1] != ' ' || line.size() > 2 + 16 + 48)
      throw std::runtime_error("Повреждён файл каналов");
    int index = line[0] - '1';
    if(seen[index]) throw std::runtime_error("Повреждён файл каналов");
    seen[index] = true;
    std::string rest = line.substr(2);
    if(withTime) {
      auto space = rest.find(' ');
      if(space == std::string::npos || space == 0) throw std::runtime_error("Повреждён файл каналов");
      std::string number = rest.substr(0, space);
      char *end = nullptr; double value = strtod(number.c_str(), &end);
      if(!end || *end || !(value >= 0) || value > 300) throw std::runtime_error("Повреждён файл каналов");
      times[index] = static_cast<float>(value); rest = rest.substr(space + 1);
    }
    if(rest.size() > 48) throw std::runtime_error("Повреждён файл каналов");
    for(unsigned char c : rest) if(c < 32 || c == 127) throw std::runtime_error("Повреждён файл каналов");
    loaded[index] = rest;
  }
  if(!in.eof()) throw std::runtime_error("Ошибка чтения файла каналов");
  names_[0].swap(loaded[0]); names_[1].swap(loaded[1]);
  primeTimes_[0] = times[0]; primeTimes_[1] = times[1];
}
void ChannelStore::commit(const std::string names[2], const float times[2]) {
  char number[2][32];
  for(int i = 0; i < 2; ++i) snprintf(number[i], sizeof(number[i]), "%.2f", times[i]);
  writeFileAtomically(path_, std::string("SMART_MIXER_CHANNELS 2\n1 ") + number[0] + " " + names[0] +
                             "\n2 " + number[1] + " " + names[1] + "\n");
}
void ChannelStore::save(int pump, const std::string &name) {
  if(pump != 1 && pump != 2) throw std::runtime_error("Насос должен быть 1 или 2");
  if(name.size() > 48) throw std::runtime_error("Слишком длинное название");
  for(unsigned char c : name) if(c < 32 || c == 127) throw std::runtime_error("Недопустимые символы в названии");
  std::string next[2] = {names_[0], names_[1]};
  next[pump - 1] = name;
  commit(next, primeTimes_);
  names_[0].swap(next[0]); names_[1].swap(next[1]);
}
void ChannelStore::savePrimeTime(int pump, float seconds) {
  if(pump != 1 && pump != 2) throw std::runtime_error("Насос должен быть 1 или 2");
  if(!(seconds >= 0) || seconds > 300) throw std::runtime_error("Время трассы должно быть 0-300 с");
  float next[2] = {primeTimes_[0], primeTimes_[1]};
  next[pump - 1] = seconds;
  commit(names_, next);
  primeTimes_[0] = next[0]; primeTimes_[1] = next[1];
}
