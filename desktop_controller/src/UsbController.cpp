#include "UsbController.h"
#include <cmath>
#include <cstdio>
#include <fcntl.h>
#include <stdexcept>
#include <termios.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>
#include <chrono>
#include <random>

using smart_mixer::Action;
using smart_mixer::Command;
namespace {
uint64_t nowMs() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
}
UsbController::~UsbController() {
  // Best effort only: existing firmware has no USB-disconnect watchdog.
  if(working_) { try { send("s"); } catch(...) {} }
  closePort();
}
void UsbController::closePort() {
  if(fd_ >= 0) ::close(fd_);
  fd_ = -1; working_ = false; recipeApi_ = false; homed_ = false; buffer_.clear();
  pumpCalibration_ = heightCalibration_ = false;
  if(cooking_) cookingMessage_ = "Связь потеряна. Проверьте остановку устройства";
  cooking_ = false;
  status_ = "State unavailable";
}
std::string UsbController::encode(const Command &c) {
  char b[80];
  const bool pumpAction = c.action == Action::Dose || c.action == Action::PumpOn ||
    c.action == Action::PumpOff || c.action == Action::Prime || c.action == Action::Measure ||
    c.action == Action::CheckDose || c.action == Action::SaveRate ||
    c.action == Action::HeightStart;
  if(pumpAction && c.pump != 1 && c.pump != 2) throw std::runtime_error("Invalid pump");
  if(!std::isfinite(c.value)) throw std::runtime_error("Invalid number");
  switch(c.action) {
  case Action::Status: return "status";
  case Action::Stop: return "s";
  case Action::Home: return "home";
  case Action::Encoder: return "act";
  case Action::HeightMark: return "hcal mark";
  case Action::HeightShow: return "hcal show";
  case Action::HeightClear: return "hcal clear";
  case Action::HeightStop: return "hcal stop";
  case Action::Move:
    if(c.value == 0 || std::abs(c.value) > 650 || std::floor(c.value) != c.value)
      throw std::runtime_error("Move must be 1..650 microsteps");
    snprintf(b, sizeof(b), "a %d", static_cast<int>(c.value)); break;
  case Action::Dose: case Action::SaveRate:
    if(c.value <= 0 || c.value > 1000) throw std::runtime_error("Value must be >0 and <=1000");
    snprintf(b, sizeof(b), "%c%d %.2f", c.action == Action::Dose ? 'd' : 'c', c.pump, c.value); break;
  case Action::PumpOn: case Action::PumpOff:
    snprintf(b, sizeof(b), "p%d %s", c.pump, c.action == Action::PumpOn ? "on" : "off"); break;
  case Action::Prime: case Action::Measure: case Action::CheckDose:
    snprintf(b, sizeof(b), "cal %s %d", c.action == Action::Prime ? "prime" : c.action == Action::Measure ? "20" : "50", c.pump); break;
  case Action::HeightStart:
    snprintf(b, sizeof(b), "hcal start %d", c.pump); break;
  default: throw std::runtime_error("Action has no wire command");
  }
  return b;
}
void UsbController::send(const std::string &s, bool log) {
  if(fd_ < 0) throw std::runtime_error("Connect Arduino first");
  const auto data = s + "\n";
  // No queue, retries or replay: a failed command must be checked by the operator.
  auto n = ::write(fd_, data.data(), data.size());
  if(n != static_cast<ssize_t>(data.size())) {
    closePort(); throw std::runtime_error("USB write failed; command will not be retried");
  }
  if(log) { log_ += "> " + s + "\n"; message_ = "Sent: " + s; }
}
void UsbController::handle(const Command &c, const char *port) {
  try {
    if(c.action == Action::Connect) {
      if(connected()) throw std::runtime_error("Disconnect first");
      fd_ = ::open(port, O_RDWR | O_NOCTTY | O_NONBLOCK | O_CLOEXEC);
      if(fd_ < 0) throw std::runtime_error(std::string("USB: ") + strerror(errno));
      termios t{};
      if(tcgetattr(fd_, &t) != 0) { closePort(); throw std::runtime_error("Not a serial port"); }
      cfmakeraw(&t); cfsetispeed(&t, B115200); cfsetospeed(&t, B115200);
      t.c_cflag |= CLOCAL | CREAD;
      t.c_cflag &= ~(CSTOPB | CRTSCTS | HUPCL);
      if(tcsetattr(fd_, TCSANOW, &t) != 0) { closePort(); throw std::runtime_error("USB setup failed"); }
      tcflush(fd_, TCIFLUSH); working_ = false; log_.clear();
      send("status"); message_ = "USB open; waiting for firmware";
      return;
    }
    if(c.action == Action::Disconnect) {
      if(working_) send("s");
      closePort(); message_ = "USB disconnected"; return;
    }
    if(c.action != Action::Status && c.action != Action::Stop && !working_)
      throw std::runtime_error("Working USB firmware not detected");
    if(cooking_ && c.action != Action::Status && c.action != Action::Stop)
      throw std::runtime_error("Сначала отмените приготовление");
    send(encode(c));
  } catch(const std::exception &e) { message_ = e.what(); }
}
void UsbController::line(const std::string &s) {
  bool wasReady = recipeReady();
  if(s.find("Запущено: ") == 0) { pumpCalibration_ = true; heightCalibration_ = false; }
  if(s.find("Замер завершён.") == 0 || s == "Калибровка остановлена") pumpCalibration_ = false;
  if(s.find("Начата калибровка высоты P") == 0 || s.find("Начата общая калибровка высоты") == 0) heightCalibration_ = true;
  if(s == "Калибровка высоты остановлена" || s.find("Таблица заполнена;") == 0 ||
     s.find("Достигнут максимум точек") == 0 || s.find("Не удалось запустить порцию:") == 0) heightCalibration_ = false;
  if(s.find("Калибровка: ") == 0) pumpCalibration_ = s != "Калибровка: не выполняется";
  if(s.find("Калибровка высоты: ") == 0) heightCalibration_ = s != "Калибровка высоты: не выполняется";
  if(s == "Остановлено" || s.find("SMART MIXER:") == 0 || s == "Homing: движение вверх...")
    pumpCalibration_ = heightCalibration_ = false;
  if((s.find("Насос 1") == 0 || s.find("Насос 2") == 0) &&
     (s.find(" включён") != std::string::npos || s.find(" выключен") != std::string::npos ||
      s.find(": дозирование ") != std::string::npos)) heightCalibration_ = false;
  log_ += "< " + s + "\n";
  if(log_.size() > 2400) {
    auto start = log_.find('\n', log_.size() - 1800);
    log_.erase(0, start == std::string::npos ? log_.size() : start + 1);
  }
  if(s.find("DIAG session=") == 0) {
    working_ = false; message_ = "Diagnostic firmware: upload desktop_usb";
  }
  if(s == "MIX_API 2") recipeApi_ = true;
  if(s.find("SMART MIXER:") == 0) { homed_ = false; recipeApi_ = false; }
  if(s == "Homing: готов" || s == "Homing завершён") homed_ = true;
  if(s == "Homing: не выполнен" || s == "Homing: движение вверх..." ||
     s == "Ошибка: верхний концевик не найден") homed_ = false;
  unsigned long operation; char state[20], reason[40], extra; int percent;
  if(cooking_ && sscanf(s.c_str(), "MIX %lu %19s %d %39s %c", &operation, state, &percent, reason, &extra) == 4 &&
     operation == operation_ && percent >= 0 && percent <= 100) {
    std::string phase = state;
    if(phase == "POSITIONING" || phase == "LOWERING" || phase == "FIRST" || phase == "SECOND" || phase == "RETURNING" ||
       phase == "DONE" || phase == "CANCELLED" || phase == "ERROR") {
      lastReport_ = nowMs(); progress_ = percent;
      if(phase == "DONE") { cooking_ = false; cookingMessage_ = "Напиток готов. Шток у края рюмки"; }
      else if(phase == "CANCELLED") { cooking_ = false; cookingMessage_ = "Приготовление отменено. Шток удерживается на месте"; }
      else if(phase == "ERROR") {
        cooking_ = false;
        std::string error = reason;
        cookingMessage_ = error == "NOT_READY" ? "Выполните поиск нуля и завершите сервисные операции" :
          error == "PUMP_CALIBRATION" ? "Нужно откалибровать производительность насосов" :
          error == "CALIBRATION_OR_DUPLICATE" ? "Проверьте таблицу высоты и объёмы рецепта. Повтор запрещён" :
          error == "LINK_TIMEOUT" ? "Потеря связи. Насосы и движение остановлены" :
          error == "BUSY" ? "Arduino занята другой операцией" :
          "Приготовление остановлено. Проверьте привод, концевик и калибровку";
      }
      else cookingMessage_ = phase == "POSITIONING" ? "Выставляю шток у края рюмки" : phase == "LOWERING" ? "Первый слой готов. Опускаю шток к жидкости" :
        phase == "FIRST" ? "Наливаю первый слой со штоком у края" :
        phase == "SECOND" ? "Наливаю второй слой; шток следует уровню" : "Налив завершён. Поднимаю шток";
    }
  }
  if(s.find("Homing:") == 0 || s.find("SMART MIXER:") == 0) {
    working_ = true; message_ = "USB ready / manual control";
  }
  if(recipeReady() && !wasReady && !cooking_) cookingMessage_ = "Arduino готова. Выберите рецепт и установите ёмкость";
  if(s.find("--- Состояние") == 0) status_.clear();
  if(s.find("Homing:") == 0 || s.find("Верхний концевик:") == 0 || s.find("Положение привода:") == 0) {
    if(status_.size() + s.size() > 700) status_.clear();
    status_ += s + "\n";
  }
}
void UsbController::poll() {
  if(fd_ < 0) return;
  char b[1024];
  // Bound work per UI iteration, so USB noise cannot stall LVGL.
  for(int i = 0; i < 4; ++i) {
    auto n = ::read(fd_, b, sizeof(b));
    if(n < 0) {
      if(errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
        closePort(); message_ = "USB lost; no commands will be replayed";
      }
      break;
    }
    if(n == 0) break;
    buffer_.append(b, n);
    size_t end;
    while((end = buffer_.find('\n')) != std::string::npos) {
      auto s = buffer_.substr(0, end); buffer_.erase(0, end + 1);
      if(!s.empty() && s.back() == '\r') s.pop_back();
      if(!s.empty()) line(s);
    }
    if(buffer_.size() > 4096) { buffer_.clear(); message_ = "USB line too long"; }
  }
  if(cooking_) {
    uint64_t now = nowMs();
    try {
      if(now - lastReport_ >= 1500) {
        send("s"); cooking_ = false;
        cookingMessage_ = "Нет подтверждения Arduino. Отправлен STOP; проверьте устройство";
      } else if(now - lastHeartbeat_ >= 250) {
        lastHeartbeat_ = now; send("lease " + std::to_string(operation_), false);
      }
    } catch(const std::exception &e) { message_ = e.what(); }
  }
}
void UsbController::prepare(const smart_mixer::Recipe &r) {
  if(!recipeReady() || cooking_ || !smart_mixer::validRecipe(r)) throw std::runtime_error("Arduino не готова к приготовлению");
  uint32_t previous = operation_;
  do { operation_ = std::random_device{}(); } while(!operation_ || operation_ == previous);
  char command[100]; snprintf(command, sizeof(command), "mix %lu %u %u %u", static_cast<unsigned long>(operation_), r.ml1, r.ml2, r.firstPump);
  send(command);
  cooking_ = true; progress_ = 0; lastHeartbeat_ = lastReport_ = nowMs();
  message_ = "Приготовление: запрос отправлен";
  cookingMessage_ = "Ожидаю подтверждения Arduino";
}
