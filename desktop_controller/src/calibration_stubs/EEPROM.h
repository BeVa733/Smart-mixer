#pragma once
#include <cstring>
#include <stdexcept>
struct TestEeprom {
  unsigned char bytes[2048];
  unsigned writes = 0;
  TestEeprom() { reset(); }
  void reset() { std::memset(bytes, 0xFF, sizeof(bytes)); writes = 0; }
  template<class T> void get(int address, T &value) {
    if(address < 0 || address + sizeof(T) > sizeof(bytes)) throw std::runtime_error("EEPROM bounds");
    std::memcpy(&value, bytes + address, sizeof(T));
  }
  template<class T> void put(int address, const T &value) {
    if(address < 0 || address + sizeof(T) > sizeof(bytes)) throw std::runtime_error("EEPROM bounds");
    std::memcpy(bytes + address, &value, sizeof(T)); ++writes;
  }
};
extern TestEeprom EEPROM;
