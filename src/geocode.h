// City search for the Weather tab: GET /api/geocode?q= is a thin proxy to Open-Meteo's
// geocoding API (plain HTTP, no key). The one outbound call made on behalf of a web
// request: it only ever runs on an explicit user action (the Search button), with a 5 s
// timeout, and refuses to start when the heap is too low.
#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

namespace geocode {

// Looks `query` up and fills out["results"] with at most 5 objects
// {name, admin1, country, lat, lon} (all text folded to ASCII). Returns false and sets
// `error` (a short sentence for the user) on failure; zero results is a success.
bool search(const char *query, JsonDocument &out, String &error);

}  // namespace geocode
