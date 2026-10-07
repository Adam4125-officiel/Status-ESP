#include "geocode.h"

#include <ESP8266HTTPClient.h>
#include <ESP8266WiFi.h>
#include <WiFiClient.h>

#include "ascii.h"
#include "net.h"

namespace geocode {

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
  ascii::fold(utf8 ? utf8 : "", buf, sizeof(buf));
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
