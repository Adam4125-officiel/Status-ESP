#include "origin_check.h"

#include <stddef.h>
#include <string.h>

namespace origin_check {

namespace {

const size_t MAX_AUTHORITY = 255;

char lower(char c) { return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c; }

struct Authority {
  const char *host;
  size_t hostLen;
  long port;   // -1 when none is written
};

// "name", "name:port", "1.2.3.4:port" or "[v6]:port". False when it is not one of these.
bool parseAuthority(const char *s, size_t n, Authority &a) {
  while (n && (*s == ' ' || *s == '\t')) { s++; n--; }
  while (n && (s[n - 1] == ' ' || s[n - 1] == '\t')) n--;
  if (n == 0 || n > MAX_AUTHORITY) return false;
  size_t hostEnd = n;
  const char *portStart = nullptr;
  if (s[0] == '[') {
    const char *close = (const char *)memchr(s, ']', n);
    if (!close) return false;
    hostEnd = (size_t)(close - s) + 1;
    if (hostEnd < n) {
      if (s[hostEnd] != ':') return false;
      portStart = s + hostEnd + 1;
    }
  } else {
    const char *colon = (const char *)memchr(s, ':', n);
    if (colon) {
      hostEnd = (size_t)(colon - s);
      portStart = colon + 1;
    }
  }
  if (hostEnd == 0) return false;
  for (size_t i = 0; i < hostEnd; i++) {
    unsigned char c = (unsigned char)s[i];
    if (c <= ' ' || c == '/' || c == '@' || c == '?' || c == '#' || c == '\\' || c >= 0x7F) return false;
  }
  a.host = s;
  a.hostLen = hostEnd;
  a.port = -1;
  if (portStart) {
    size_t plen = (size_t)(s + n - portStart);
    if (plen == 0 || plen > 5) return false;
    long p = 0;
    for (size_t i = 0; i < plen; i++) {
      if (portStart[i] < '0' || portStart[i] > '9') return false;
      p = p * 10 + (portStart[i] - '0');
    }
    if (p > 65535) return false;
    a.port = p;
  }
  return true;
}

bool startsWithNoCase(const char *s, const char *prefix) {
  for (; *prefix; s++, prefix++) {
    if (lower(*s) != *prefix) return false;   // (a NUL in s never equals a prefix letter)
  }
  return true;
}

}  // namespace

bool allowed(const char *origin, const char *host, bool isAp) {
  if (isAp) return true;
  if (!origin || !*origin) return true;   // no Origin: not a browser's cross-site request
  if (!host) return false;

  long defaultPort;
  if (startsWithNoCase(origin, "http://")) {
    origin += 7;
    defaultPort = 80;
  } else if (startsWithNoCase(origin, "https://")) {
    origin += 8;
    defaultPort = 443;
  } else {
    return false;   // "null", "file://", garbage
  }
  Authority o, h;
  size_t olen = strnlen(origin, MAX_AUTHORITY + 1);
  if (!parseAuthority(origin, olen, o)) return false;
  if (!parseAuthority(host, strnlen(host, MAX_AUTHORITY + 1), h)) return false;

  if (o.hostLen != h.hostLen) return false;
  for (size_t i = 0; i < o.hostLen; i++) {
    if (lower(o.host[i]) != lower(h.host[i])) return false;
  }
  long originPort = o.port < 0 ? defaultPort : o.port;
  // A Host without a port is the scheme's default (a reverse proxy that terminates TLS, or port 80).
  if (h.port < 0) return originPort == 80 || originPort == 443;
  return originPort == h.port;
}

}  // namespace origin_check
