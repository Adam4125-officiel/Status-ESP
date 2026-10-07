#include "mdns_dns.h"

#include <string.h>

namespace mdns_dns {

namespace {

const uint16_t TYPE_A = 1, TYPE_AAAA = 28, TYPE_NSEC = 47, TYPE_ANY = 255, CLASS_IN = 1;
const uint32_t TTL_NORMAL = 120, TTL_LEGACY = 10;

uint16_t get16(const uint8_t *p) { return (uint16_t)((p[0] << 8) | p[1]); }

char lower(char c) { return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c; }

// True when the name at `pos` is "<host>.local". `end` is set to the offset just after the name
// field in the message (a compression pointer ends it after its two bytes). A pointer may only
// go backwards and at most a few hops, so a hostile message cannot loop.
bool matchName(const uint8_t *q, size_t length, size_t pos, const char *host, size_t &end) {
  const char *labels[2] = {host, "local"};
  int index = 0;
  bool jumped = false, ok = true;
  int hops = 0;
  end = 0;
  for (;;) {
    if (pos >= length) return false;
    uint8_t len = q[pos];
    if ((len & 0xC0) == 0xC0) {
      if (pos + 1 >= length) return false;
      size_t target = (size_t)(((len & 0x3F) << 8) | q[pos + 1]);
      if (!jumped) end = pos + 2;
      jumped = true;
      if (target >= pos || ++hops > 4) return false;
      pos = target;
      continue;
    }
    if (len & 0xC0) return false;  // reserved label types
    if (len == 0) {
      if (!jumped) end = pos + 1;
      return ok && index == 2;
    }
    if (pos + 1 + len > length) return false;
    if (ok) {
      if (index >= 2 || strlen(labels[index]) != len) {
        ok = false;
      } else {
        for (uint8_t i = 0; i < len; i++) {
          if (lower((char)q[pos + 1 + i]) != lower(labels[index][i])) ok = false;
        }
      }
      index++;
    }
    pos += 1 + (size_t)len;
  }
}

struct Writer {
  uint8_t *buf;
  size_t cap, len;
  bool full;
  void put(uint8_t b) {
    if (len < cap) buf[len++] = b;
    else full = true;
  }
  void put16(uint16_t v) { put((uint8_t)(v >> 8)); put((uint8_t)v); }
  void put32(uint32_t v) { put16((uint16_t)(v >> 16)); put16((uint16_t)v); }
  void name(const char *host) {
    size_t n = strlen(host);
    put((uint8_t)n);
    for (size_t i = 0; i < n; i++) put((uint8_t)host[i]);
    put(5);
    for (const char *p = "local"; *p; p++) put((uint8_t)*p);
    put(0);
  }
};

void answerA(Writer &w, const char *host, const uint8_t ip[4], uint16_t cls, uint32_t ttl) {
  w.name(host);
  w.put16(TYPE_A);
  w.put16(cls);
  w.put32(ttl);
  w.put16(4);
  for (int i = 0; i < 4; i++) w.put(ip[i]);
}

void answerNsec(Writer &w, const char *host) {
  w.name(host);
  w.put16(TYPE_NSEC);
  w.put16(0x8000 | CLASS_IN);
  w.put32(TTL_NORMAL);
  w.put16((uint16_t)(strlen(host) + 8 + 3));   // next name (host.local) + window 0, length 1, 1 byte
  w.name(host);
  w.put(0);      // window block 0
  w.put(1);      // bitmap length
  w.put(0x40);   // type 1 (A) is the only one that exists
}

}  // namespace

size_t reply(const uint8_t *q, size_t length, const char *host, const uint8_t ip[4], bool legacy, uint8_t *out,
             size_t cap) {
  if (length < 12) return 0;
  uint16_t flags = get16(q + 2);
  if ((flags & 0x8000) || ((flags >> 11) & 0xF) != 0) return 0;   // a response, or not a standard query
  uint16_t questions = get16(q + 4);
  size_t pos = 12;
  bool wantA = false, wantAaaa = false;
  uint16_t firstType = 0, firstClass = 0;
  for (uint16_t i = 0; i < questions && i < 8; i++) {
    size_t end;
    bool match = matchName(q, length, pos, host, end);
    if (end == 0 || end + 4 > length) break;
    uint16_t type = get16(q + end), cls = get16(q + end + 2) & 0x7FFF;   // top bit: "unicast answer wanted"
    pos = end + 4;
    if (!match || (cls != CLASS_IN && cls != TYPE_ANY)) continue;
    if (type == TYPE_A || type == TYPE_ANY) {
      if (!wantA) { firstType = type; firstClass = cls; }
      wantA = true;
    } else if (type == TYPE_AAAA) {
      wantAaaa = true;
    }
  }
  if (legacy) wantAaaa = false;
  if (!wantA && !wantAaaa) return 0;

  Writer w = {out, cap, 0, false};
  w.put16(legacy ? get16(q) : 0);
  w.put16(0x8400);                                  // response, authoritative
  w.put16(legacy ? 1 : 0);                          // questions echoed for a legacy resolver
  w.put16((uint16_t)((wantA ? 1 : 0) + (wantAaaa ? 1 : 0)));
  w.put16(0);
  w.put16(0);
  if (legacy) {
    w.name(host);
    w.put16(firstType);
    w.put16(firstClass);
  }
  if (wantA) answerA(w, host, ip, legacy ? CLASS_IN : (uint16_t)(0x8000 | CLASS_IN), legacy ? TTL_LEGACY : TTL_NORMAL);
  if (wantAaaa) answerNsec(w, host);
  return w.full ? 0 : w.len;
}

size_t announce(const char *host, const uint8_t ip[4], uint8_t *out, size_t cap) {
  Writer w = {out, cap, 0, false};
  w.put16(0);
  w.put16(0x8400);
  w.put16(0);
  w.put16(1);
  w.put16(0);
  w.put16(0);
  answerA(w, host, ip, 0x8000 | CLASS_IN, TTL_NORMAL);
  return w.full ? 0 : w.len;
}

}  // namespace mdns_dns
