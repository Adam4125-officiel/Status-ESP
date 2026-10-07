// Web server: the single-page interface, the JSON API, file uploads and the unchanged
// /update, /v.json, /reboot, /set and /wifi routes. See the routes table in web.cpp.
#pragma once

#include <Arduino.h>

namespace web {

// setup(): registers every route. /update's custom GET page is registered BEFORE
// updater.setup() (the server takes the first handler that matches), and the form that
// would erase the file system never comes back. Does not start listening yet.
void begin();
// Every loop() pass: starts listening once the network state is settled (connected or
// rescue AP), then serves clients. Never blocks for long.
void loop();

}  // namespace web
