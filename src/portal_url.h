// The Status-Portal address and key as a person types them: checked and normalised before they are
// stored, so the client only ever sees "http://host[:port]" and a key that fits an HTTP header.
// Pure functions, no dependency on the device: tools/test_host.sh builds them on a PC.
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace portal {

const size_t MAX_URL = 63;   // "http://host[:port]" and nothing else
const size_t MAX_KEY = 64;   // Status-Portal's own keys are 48 hex characters

enum UrlCheck : uint8_t {
  URL_OK = 0,
  URL_HTTPS,         // https://: the ESP8266 cannot do TLS
  URL_SCHEME,        // some other scheme (ftp://, ...)
  URL_CREDENTIALS,   // user:password@host
  URL_PATH,          // anything after the port: a path, a query, a fragment
  URL_HOST,          // empty, too long, or not a plain host name / IPv4 address
  URL_PORT,          // not 1..65535
  URL_TOO_LONG
};

// Checks and normalises what was typed: "192.0.2.10:5000", " http://nas.lan:5000/ " ->
// "http://192.0.2.10:5000", "http://nas.lan:5000". A missing scheme means http, a trailing slash is
// dropped. `out` (cap bytes) is only written on URL_OK; anything else says why it was refused.
UrlCheck checkUrl(const char *in, char *out, size_t cap);

// 8..64 printable ASCII characters, no space: what can go in an HTTP header value unharmed.
bool validKey(const char *key);

}  // namespace portal
