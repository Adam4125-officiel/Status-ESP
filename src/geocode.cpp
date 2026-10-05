#include "geocode.h"

#include <ESP8266HTTPClient.h>
#include <ESP8266WiFi.h>
#include <WiFiClient.h>

#include "net.h"

namespace geocode {

// Code points U+00C0..U+017F, two characters each (' ' = nothing), generated once.
static const char FOLD_TABLE[] PROGMEM =
    "A A A A A A AEC E E E E I I I I D N O O O O O x O U U U U Y Thss"
    "a a a a a a aec e e e e i i i i d n o o o o o / o u u u u y thy "
    "A a A a A a C c C c C c C c D d D d E e E e E e E e E e G g G g "
    "G g G g H h H h I i I i I i I i I i IJijJ j K k k L l L l L l L "
    "l L l N n N n N n n N n O o O o O o OEoeR r R r R r S s S s S s "
    "S s T t T t T t U u U u U u U u U u U u W w Y y Y Z z Z z Z z s ";

void toAscii(const char *in, char *out, size_t cap) {
  if (cap == 0) return;
  size_t n = 0;
  auto put = [&](char c) {
    if (n + 1 < cap) out[n++] = c;
  };
  while (*in) {
    uint8_t c = (uint8_t)*in++;
    uint32_t cp;
    if (c < 0x80) {
      cp = c;
    } else if ((c & 0xE0) == 0xC0 && (*in & 0xC0) == 0x80) {
      cp = ((uint32_t)(c & 0x1F) << 6) | (*in++ & 0x3F);
    } else if ((c & 0xF0) == 0xE0 && (in[0] & 0xC0) == 0x80 && (in[1] & 0xC0) == 0x80) {
      cp = ((uint32_t)(c & 0x0F) << 12) | ((uint32_t)(in[0] & 0x3F) << 6) | (in[1] & 0x3F);
      in += 2;
    } else {
      // 4-byte sequence or garbage: skip the continuation bytes, nothing to draw anyway
      while ((*in & 0xC0) == 0x80) in++;
      continue;
    }
    if (cp >= 32 && cp < 127) {
      put((char)cp);
    } else if (cp >= 0xC0 && cp <= 0x17F) {
      uint16_t i = (uint16_t)(cp - 0xC0) * 2;
      char a = (char)pgm_read_byte(FOLD_TABLE + i);
      char b = (char)pgm_read_byte(FOLD_TABLE + i + 1);
      if (a != ' ') put(a);
      if (b != ' ') put(b);
    } else if (cp == 0x2010 || cp == 0x2011 || cp == 0x2013 || cp == 0x2014) {
      put('-');
    } else if (cp == 0x2018 || cp == 0x2019) {
      put('\'');
    }
    // anything else (Cyrillic, CJK, ...) has no ASCII form: dropped
  }
  while (n > 0 && out[n - 1] == ' ') n--;
  out[n] = '\0';
}

static void percentEncode(const char *s, String &out) {
  static const char DIGITS[] = "0123456789ABCDEF";
  for (; *s; s++) {
    uint8_t c = (uint8_t)*s;
    if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_' ||
        c == '.' || c == '~') {
      out += (char)c;
    } else {
      out += '%';
      out += DIGITS[c >> 4];
      out += DIGITS[c & 15];
    }
  }
}

static void addText(JsonObject o, const char *key, const char *utf8) {
  char buf[48];
  toAscii(utf8 ? utf8 : "", buf, sizeof(buf));
  o[key] = String(buf);
}

bool search(const char *query, JsonDocument &out, String &error) {
  if (!net::isConnected()) {
    error = net::isAp() ? "No internet in rescue mode: connect the device to your Wi-Fi first"
                        : "Not connected to Wi-Fi";
    return false;
  }
  if (ESP.getMaxFreeBlockSize() < 9000) {
    error = "Not enough free memory right now, try again in a moment";
    return false;
  }

  String url = F("http://geocoding-api.open-meteo.com/v1/search?count=5&language=en&format=json&name=");
  percentEncode(query, url);

  WiFiClient client;
  HTTPClient http;
  http.setTimeout(5000);
  http.useHTTP10(true);  // no chunked encoding: getStream() is the raw body
  if (!http.begin(client, url)) {
    error = "Could not start the request";
    return false;
  }
  int code = http.GET();
  if (code != HTTP_CODE_OK) {
    http.end();
    error = code < 0 ? "The weather service did not answer (timeout or no internet)"
                     : String("The weather service answered HTTP ") + code;
    return false;
  }

  JsonDocument filter;
  filter["results"][0]["name"] = true;
  filter["results"][0]["admin1"] = true;
  filter["results"][0]["country"] = true;
  filter["results"][0]["latitude"] = true;
  filter["results"][0]["longitude"] = true;

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, http.getStream(), DeserializationOption::Filter(filter));
  http.end();
  if (err) {
    error = "Unreadable answer from the weather service";
    return false;
  }

  JsonArray results = out["results"].to<JsonArray>();
  for (JsonObject r : doc["results"].as<JsonArray>()) {
    if (results.size() >= 5) break;
    if (!r["latitude"].is<float>() || !r["longitude"].is<float>()) continue;
    JsonObject o = results.add<JsonObject>();
    addText(o, "name", r["name"] | "");
    if (o["name"].as<String>().length() == 0) o["name"] = "Unnamed place";
    addText(o, "admin1", r["admin1"] | "");
    addText(o, "country", r["country"] | "");
    o["lat"] = r["latitude"].as<float>();
    o["lon"] = r["longitude"].as<float>();
  }
  return true;
}

}  // namespace geocode
