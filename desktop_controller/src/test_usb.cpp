#include "UsbController.h"
#include <fcntl.h>
#include <pty.h>
#include <unistd.h>
#include <stdexcept>
#include <cstdio>
#include <cmath>
using namespace smart_mixer;
void check(bool value, const char *message) { if(!value) throw std::runtime_error(message); }
std::string readPeer(int fd) {
  std::string result;
  for(int i = 0; i < 100; ++i) {
    char b[100]; auto n = read(fd, b, sizeof(b));
    if(n > 0) result.append(b, n);
    if(result.find('\n') != std::string::npos) break;
    usleep(1000);
  }
  return result;
}
int main() {
  int master, slave; char name[128];
  check(openpty(&master, &slave, name, nullptr, nullptr) == 0, "openpty");
  fcntl(master, F_SETFL, O_NONBLOCK);
  UsbController usb;
  usb.handle({Action::Connect, 0, 0}, name);
  check(usb.connected(), "connect");
  check(readPeer(master) == "status\n", "initial status only");
  usb.handle({Action::Dose, 1, 5}, name);
  check(readPeer(master).empty(), "unidentified firmware must not dose");
  const char diag[] = "DIAG session=0 rx=0\n";
  write(master, diag, sizeof(diag) - 1); usleep(2000); usb.poll();
  check(!usb.working(), "diagnostics gate");
  usb.handle({Action::Move, 0, -10}, name);
  check(readPeer(master).empty(), "diagnostics cannot move");
  const char state[] = "Homing: готов\nВерхний концевик: активен\n";
  write(master, state, 9); usleep(2000); usb.poll();
  check(!usb.working(), "partial line not yet accepted");
  write(master, state + 9, sizeof(state) - 10); usleep(2000); usb.poll();
  check(usb.working(), "legacy identified from complete line");
  auto reportCalibration = [&](const char *line) {
    std::string report = std::string(line) + "\n";
    write(master, report.data(), report.size()); usleep(2000); usb.poll();
  };
  check(!usb.calibrating(), "calibration initially inactive");
  reportCalibration("Запущено: P1 20 SEC"); check(usb.calibrating(), "pump calibration acknowledged");
  reportCalibration("Замер завершён. Взвесь воду и введи: c1"); check(!usb.calibrating(), "pump calibration completed");
  reportCalibration("Начата калибровка высоты P1"); check(usb.calibrating(), "height calibration acknowledged");
  reportCalibration("Насос P1: наливаю 5 мл"); check(usb.calibrating(), "height dose does not terminate calibration");
  reportCalibration("Насос 1: сохранено 1.300 мл/с"); check(usb.calibrating(), "saving rate does not hide active calibration stop");
  reportCalibration("Калибровка высоты остановлена"); check(!usb.calibrating(), "height calibration ended");
  reportCalibration("Калибровка: P2 PRIME"); check(usb.calibrating(), "status restores calibration state");
  reportCalibration("Остановлено"); check(!usb.calibrating(), "STOP acknowledgement clears calibration state");
  usb.handle({Action::Dose, 2, 5.5}, name);
  check(readPeer(master) == "d2 5.50\n", "exact dose wire mapping");
  reportCalibration("P2: 0.000 мл/с, выключен");
  usb.handle({Action::Dose, 2, 50}, name);
  check(readPeer(master).empty() && usb.message().find("не откалиброван") != std::string::npos,
        "uncalibrated pump dose is explained instead of silently refused");
  check(usb.status().find("P2: 0.000") != std::string::npos, "pump rates shown in state");
  usb.handle({Action::PumpOn, 2, 0}, name); check(readPeer(master) == "p2 on\n", "continuous run needs no calibration");
  reportCalibration("Насос 2: сохранено 1.197 мл/с");
  usb.handle({Action::Dose, 2, 50}, name); check(readPeer(master) == "d2 50.00\n", "dose allowed after saving rate");
  Recipe recipe; recipe.name = "Тестовый"; recipe.ml1 = 20; recipe.ml2 = 30; recipe.firstPump = 2;
  bool rejectedOldFirmware = false;
  try { usb.prepare(recipe); } catch(const std::exception &) { rejectedOldFirmware = true; }
  check(rejectedOldFirmware, "no automatic cooking on unsupported firmware");
  reportCalibration("MIX_API 1");
  check(!usb.recipeReady(), "old API height-table argument must not be mistaken for pump order");
  const char capability[] = "MIX_API 2\n";
  write(master, capability, sizeof(capability) - 1); usleep(2000); usb.poll();
  check(usb.recipeReady(), "recipe capability detection");
  usb.prepare(recipe); auto wire = readPeer(master);
  unsigned long operation; unsigned ml1, ml2, profile;
  check(sscanf(wire.c_str(), "mix %lu %u %u %u", &operation, &ml1, &ml2, &profile) == 4 &&
        operation && ml1 == 20 && ml2 == 30 && profile == 2, "physical volumes and selected first pump wire mapping");
  usb.handle({Action::Move, 0, -5}, name); check(readPeer(master).empty(), "manual command blocked while cooking");
  char report[100];
  int length = snprintf(report, sizeof(report), "MIX %lu DONE 100 NONE\n", operation + 1);
  write(master, report, length); usleep(2000); usb.poll(); check(usb.cooking(), "unrelated DONE ignored");
  usleep(260000); usb.poll();
  check(readPeer(master) == "lease " + std::to_string(operation) + "\n", "heartbeat without resending recipe");
  length = snprintf(report, sizeof(report), "MIX %lu FIRST 10 NONE\n", operation);
  write(master, report, length); usleep(2000); usb.poll(); check(usb.progress() == 10, "confirmed recipe progress");
  usb.handle({Action::Stop, 0, 0}, name); check(readPeer(master) == "s\n", "cancel request");
  check(usb.cooking(), "STOP write is not a cancellation confirmation");
  length = snprintf(report, sizeof(report), "MIX %lu CANCELLED 10 NONE\n", operation);
  write(master, report, length); usleep(2000); usb.poll(); check(!usb.cooking(), "cancel confirmed");
  usb.prepare(recipe); wire = readPeer(master);
  unsigned long next;
  check(sscanf(wire.c_str(), "mix %lu", &next) == 1 && next != operation, "new recipe needs new operation id");
  length = snprintf(report, sizeof(report), "MIX %lu DONE 100 NONE\n", next);
  write(master, report, length); usleep(2000); usb.poll();
  check(!usb.cooking() && usb.progress() == 100, "DONE confirms recipe completion");
  Recipe mixed = recipe; mixed.layered = false;
  bool rejectedMixed = false;
  try { usb.prepare(mixed); } catch(const std::exception &) { rejectedMixed = true; }
  check(rejectedMixed && readPeer(master).empty(), "API 2 firmware cannot run a mixed recipe");
  reportCalibration("MIX_API 3"); check(usb.recipeReady(), "API 3 capability");
  usb.prepare(mixed); wire = readPeer(master);
  unsigned layered = 9;
  check(sscanf(wire.c_str(), "mix %lu %u %u %u %u", &next, &ml1, &ml2, &profile, &layered) == 5 &&
        ml1 == 20 && ml2 == 30 && profile == 2 && layered == 0, "API 3 sends the recipe mode");
  length = snprintf(report, sizeof(report), "MIX %lu SECOND 60 NONE\n", next);
  write(master, report, length); usleep(2000); usb.poll();
  check(usb.cookingMessage().find("компонент") != std::string::npos, "mixed recipe progress is not described as layers");
  length = snprintf(report, sizeof(report), "MIX %lu DONE 100 NONE\n", next);
  write(master, report, length); usleep(2000); usb.poll(); check(!usb.cooking(), "mixed DONE");
  usb.prepare(recipe); wire = readPeer(master);
  check(sscanf(wire.c_str(), "mix %lu %u %u %u %u", &next, &ml1, &ml2, &profile, &layered) == 5 && layered == 1, "API 3 layered recipe");
  length = snprintf(report, sizeof(report), "MIX %lu DONE 100 NONE\n", next);
  write(master, report, length); usleep(2000); usb.poll(); check(!usb.cooking(), "layered DONE on API 3");
  int primedPump = 0; float primedSeconds = 0;
  usb.handle({Action::PrimeMeasured, 1, 0}, name);
  check(readPeer(master) == "s\n" && !usb.takePrimeMeasurement(primedPump, primedSeconds), "no measurement without started priming");
  usb.handle({Action::Prime, 1, 0}, name); check(readPeer(master) == "cal prime 1\n", "priming calibration wire");
  usleep(300000); reportCalibration("Запущено: P1 PRIME");
  usleep(700000);
  usb.handle({Action::PrimeMeasured, 1, 0}, name); check(readPeer(master) == "s\n", "liquid arrival stops pump");
  check(usb.takePrimeMeasurement(primedPump, primedSeconds) && primedPump == 1 &&
        primedSeconds > 0.6f && primedSeconds < 1.0f, "line time counted from Arduino start acknowledgement");
  check(!usb.takePrimeMeasurement(primedPump, primedSeconds), "measurement returned once");
  usb.handle({Action::Prime, 2, 0}, name); readPeer(master);
  reportCalibration("Запущено: P2 PRIME"); reportCalibration("Остановлено");
  usb.handle({Action::PrimeMeasured, 2, 0}, name); readPeer(master);
  check(!usb.takePrimeMeasurement(primedPump, primedSeconds), "stopped priming is not a measurement");
  usb.handle({Action::RunFor, 2, 12.5f}, name); check(readPeer(master) == "t2 12.5\n", "timed run wire");
  usb.handle({Action::Disconnect, 0, 0}, name);
  check(readPeer(master) == "s\n", "disconnect best effort stop");
  check(!usb.connected(), "disconnected");
  usb.handle({Action::Connect, 0, 0}, name);
  check(readPeer(master) == "status\n", "reconnect must not replay dose");
  check(!usb.working(), "reconnect needs fresh identification");
  check(!usb.recipeReady(), "reconnect forgets old capability");
  usb.handle({Action::Connect, 0, 0}, name); readPeer(master);
  reportCalibration("Homing: готов"); reportCalibration("MIX_API 2");
  usb.handle({Action::RunFor, 1, 5}, name);
  check(readPeer(master).empty(), "timed run needs API 3 firmware");
  usb.handle({Action::Disconnect, 0, 0}, name); readPeer(master);
  check(UsbController::encode({Action::HeightShow, 0, 0}) == "hcal show" &&
        UsbController::encode({Action::HeightClear, 0, 0}) == "hcal clear", "common height commands have no pump selector");
  check(UsbController::encode({Action::Move, 0, -9999}) == "a -9999", "long downward move allowed");
  for(auto c : {Command{Action::Move, 0, 10000}, Command{Action::Dose, 3, 5},
                Command{Action::RunFor, 1, 0}, Command{Action::RunFor, 1, 601}, Command{Action::RunFor, 3, 5},
                Command{Action::Dose, 1, NAN}, Command{Action::SaveRate, 1, 0}}) {
    bool rejected = false;
    try { UsbController::encode(c); } catch(const std::exception &) { rejected = true; }
    check(rejected, "invalid command rejected");
  }
  usb.handle({Action::Disconnect, 0, 0}, name);
  close(master); close(slave);
  puts("USB PTY: gating, UTF-8 framing, dose, disconnect, no replay, validation passed.");
}
