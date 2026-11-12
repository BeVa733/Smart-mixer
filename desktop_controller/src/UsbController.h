#pragma once
#include <MixerUi.h>
#include <string>

class UsbController {
public:
  ~UsbController();
  void handle(const smart_mixer::Command &, const char *port);
  void poll();
  bool connected() const { return fd_ >= 0; }
  bool working() const { return working_; }
  const std::string &message() const { return message_; }
  const std::string &status() const { return status_; }
  const std::string &log() const { return log_; }
  static std::string encode(const smart_mixer::Command &);
  void prepare(const smart_mixer::Recipe &recipe);
  bool recipeReady() const { return working_ && recipeApi_ >= 2 && homed_ && !calibrating(); }
  bool cooking() const { return cooking_; }
  bool calibrating() const { return pumpCalibration_ || heightCalibration_; }
  int progress() const { return progress_; }
  // A finished line-priming calibration, returned once.
  bool takePrimeMeasurement(int &pump, float &seconds);
  // Height table received after "hcal show", returned once.
  bool takeHeightTable(std::string &table);
  const std::string &cookingMessage() const { return cookingMessage_; }
private:
  int fd_ = -1;
  bool working_ = false;
  int recipeApi_ = 0; // 2: layered only, 3: layered or mixed
  bool cooking_ = false, homed_ = false, cookingLayered_ = true;
  bool pumpCalibration_ = false, heightCalibration_ = false;
  uint32_t operation_ = 0;
  int primeCalPump_ = 0, measuredPump_ = 0;
  // Flow rates reported by Arduino, ml/s; negative = not reported yet.
  float rates_[2] = {-1, -1};
  std::string heightTable_;
  bool collectingTable_ = false, tableReady_ = false;
  uint64_t tableAt_ = 0;
  uint64_t primeStartedAt_ = 0;
  float measuredSeconds_ = 0;
  uint64_t lastHeartbeat_ = 0, lastReport_ = 0;
  int progress_ = 0;
  std::string cookingMessage_ = "Подключите Arduino и выполните поиск нуля в сервисном режиме";
  std::string buffer_, log_, message_ = "USB disconnected", status_ = "State unavailable";
  void closePort();
  void send(const std::string &, bool log = true);
  void line(const std::string &);
};
