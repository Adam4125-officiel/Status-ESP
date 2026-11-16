// Stand-in for the Arduino core for test_backup.cpp: a String that ArduinoJson accepts, F(), yield(), ESP.
// It lives in its own folder (put first on the include path) because the other host tests use a smaller one.
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include <string>

#define PROGMEM
#define F(x) (x)
#define PSTR(x) (x)

class String {
 public:
  String() {}
  String(const char *s) : s_(s ? s : "") {}
  String(const String &o) : s_(o.s_) {}
  unsigned int length() const { return (unsigned int)s_.size(); }
  const char *c_str() const { return s_.c_str(); }
  bool isEmpty() const { return s_.empty(); }
  bool reserve(unsigned int n) { s_.reserve(n); return true; }
  bool concat(const char *s, unsigned int n) { s_.append(s, n); return true; }
  bool concat(const char *s) { s_ += s; return true; }
  bool concat(char c) { s_ += c; return true; }
  String &operator=(const char *s) { s_ = s ? s : ""; return *this; }
  String &operator=(const String &o) { s_ = o.s_; return *this; }
  String &operator+=(const char *s) { s_ += s; return *this; }
  String &operator+=(const String &o) { s_ += o.s_; return *this; }
  bool operator==(const String &o) const { return s_ == o.s_; }
  char operator[](unsigned int i) const { return s_[i]; }
  friend String operator+(const String &a, const String &b) { String r; r.s_ = a.s_ + b.s_; return r; }
  friend String operator+(const String &a, const char *b) { String r; r.s_ = a.s_ + b; return r; }
  friend String operator+(const char *a, const String &b) { String r; r.s_ = std::string(a) + b.s_; return r; }

 private:
  std::string s_;
};

inline void yield() {}

struct EspClass {
  uint32_t getMaxFreeBlockSize() { return 30000; }
  uint32_t getFreeHeap() { return 30000; }
};
extern EspClass ESP;
