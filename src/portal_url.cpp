#include "portal_url.h"

#include <string.h>
#include <strings.h>

namespace portal {

bool validKey(const char *key) {
  size_t n = key ? strlen(key) : 0;
  if (n < 8 || n > MAX_KEY) return false;
  for (size_t i = 0; i < n; i++) {
    if ((uint8_t)key[i] < 33 || (uint8_t)key[i] > 126) return false;
  }
  return true;
}

UrlCheck checkUrl(const char *in, char *out, size_t cap) {
  while (*in == ' ') in++;
  size_t n = strlen(in);
  while (n > 0 && in[n - 1] == ' ') n--;
  if (n >= 8 && strncasecmp(in, "https://", 8) == 0) return URL_HTTPS;
  if (n >= 7 && strncasecmp(in, "http://", 7) == 0) {
    in += 7;
    n -= 7;
  } else {
    for (size_t i = 0; i + 2 < n; i++) {
      if (in[i] == ':' && in[i + 1] == '/' && in[i + 2] == '/') return URL_SCHEME;   // ftp://, ws://, ...
    }
  }
  // Now host[:port] followed by nothing, or by slashes only.
  size_t end = 0;
  while (end < n && in[end] != '/' && in[end] != '?' && in[end] != '#') end++;
  for (size_t i = end; i < n; i++) {
    if (in[i] != '/') return URL_PATH;   // a path, a query or a fragment
  }
  for (size_t i = 0; i < end; i++) {
    if (in[i] == '@') return URL_CREDENTIALS;
  }
  size_t colon = end;
  for (size_t i = 0; i < end; i++) {
    if (in[i] == ':') {
      colon = i;
      break;
    }
  }
  if (colon == 0 || colon > 40) return URL_HOST;
  for (size_t i = 0; i < colon; i++) {
    char c = in[i];
    bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '-' || c == '_';
    if (!ok) return URL_HOST;   // an IPv6 literal, a space, anything odd
  }
  if (colon < end) {
    size_t digits = end - colon - 1;
    if (digits < 1 || digits > 5) return URL_PORT;
    long port = 0;
    for (size_t i = colon + 1; i < end; i++) {
      if (in[i] < '0' || in[i] > '9') return URL_PORT;
      port = port * 10 + (in[i] - '0');
    }
    if (port < 1 || port > 65535) return URL_PORT;
  }
  if (7 + end + 1 > cap) return URL_TOO_LONG;
  memcpy(out, "http://", 7);
  memcpy(out + 7, in, end);
  out[7 + end] = '\0';
  return URL_OK;
}

}  // namespace portal
