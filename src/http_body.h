// Collects an HTTP answer in a String, refusing more than `max` bytes. It is what
// HTTPClient::writeToPrint() writes into: the request is plain HTTP, so whatever answers is not
// necessarily who was asked, and a wrong answer must not be able to eat the heap.
//
// Reading the body this way (and not with a hand-written available() / read() loop over
// getStreamPtr(), which saw the transfer end after one or two TCP segments on the real device) is
// the proven approach: see "Known pitfalls" in CLAUDE.md.
#pragma once

#include <Arduino.h>

class BodySink : public Print {
 public:
  explicit BodySink(size_t max) : max_(max) {}

  String text;
  bool overflow = false;

  size_t write(uint8_t c) override { return write(&c, 1); }
  size_t write(const uint8_t *data, size_t n) override {
    if (text.length() + n > max_) {
      overflow = true;
      return 0;
    }
    text.concat((const char *)data, (unsigned int)n);
    return n;
  }
  int availableForWrite() override { return (int)(max_ - text.length()); }

 private:
  size_t max_;
};
