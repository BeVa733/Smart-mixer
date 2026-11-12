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
  bool recipeReady() const { return working_ && recipeApi_ && homed_ && !calibrating(); }
  bool cooking() const { return cooking_; }
  bool calibrating() const { return pumpCalibration_ || heightCalibration_; }
  int progress() const { return progress_; }
  const std::string &cookingMessage() const { return cookingMessage_; }
private:
  int fd_ = -1;
  bool working_ = false;
  bool recipeApi_ = false, cooking_ = false, homed_ = false;
  bool pumpCalibration_ = false, heightCalibration_ = false;
  uint32_t operation_ = 0;
  uint64_t lastHeartbeat_ = 0, lastReport_ = 0;
  int progress_ = 0;
  std::string cookingMessage_ = "Подключите Arduino и выполните поиск нуля в сервисном режиме";
  std::string buffer_, log_, message_ = "USB disconnected", status_ = "State unavailable";
  void closePort();
  void send(const std::string &, bool log = true);
  void line(const std::string &);
};
