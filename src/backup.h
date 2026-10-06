// Full file-system backup and restore, as one uncompressed tar.
//
// Backup: every LittleFS file (the stock firmware's files, /config.json, /custom.json, the
// pictures and GIFs...) plus two virtual entries: `status-esp-backup.json` first (a marker, so a
// restore can tell one of our archives from any other file) and `status-esp-wifi.json`, holding
// the Wi-Fi network stored in the SDK's own flash area (ssid and password). The stock firmware
// has a real /wifi.json of its own, hence the longer name. The tar is generated on the fly,
// block by block: no buffer is as big as a file, and nothing is written to flash. The size is
// known beforehand (tarSize()), so the HTTP answer has a Content-Length and a progress bar.
//
// The archive therefore holds SECRETS: the Wi-Fi password twice (the stock /config.json and the
// virtual entry) and, if one is set, the web password (inside /custom.json). The web interface
// says so, and the download is refused in rescue access-point mode, where the hotspot is open.
//
// Restore: the multipart upload is fed to restoreFeed() chunk by chunk, a state machine that
// never needs more than one 512-byte header block in RAM. Each file is written to a temporary
// name first and renamed over its destination once complete (an interrupted restore never
// leaves half a file in place); it is skipped when it does not fit in the free space, when its
// name is not a clean relative path (".." and friends), or when it is not a regular file. An
// archive whose first entry is not the marker is refused before anything is written.
// LittleFS is never formatted, and nothing is deleted. This is the ONE place that rewrites the
// stock files, and only with what the owner's own backup holds (CLAUDE.md, rule 2).
#pragma once

#include <Arduino.h>

namespace backup {

// Name of the virtual archive entry that carries the SDK's Wi-Fi credentials.
extern const char WIFI_ENTRY[];

// ---- Backup ----

// Called with each piece of the archive; returns false when the receiver is gone.
typedef bool (*Sink)(void *ctx, const uint8_t *data, size_t len);

// Exact number of bytes writeTar() will produce.
size_t tarSize();
// Streams the archive. Returns false if the sink refused data (the client went away) or there
// was not enough memory to start; nothing in the file system is modified either way.
bool writeTar(Sink sink, void *ctx);

// ---- Restore ----

struct Result {
  bool ok;                // the archive was read to its end without a fatal error
  uint16_t restored;      // files written
  uint16_t skipped;       // entries left alone (does not fit, unsafe name, not a regular file...)
  bool haveWifi;          // the archive carried a Wi-Fi network (ssid / pass below)
  char ssid[33];
  char pass[65];
  char error[56];         // why it stopped, when !ok
};

// Starts a restore. False when there is not enough memory or the file system is not mounted.
bool restoreBegin();
// Feeds the next piece of the uploaded archive. False on a fatal error (see restoreFinish()).
bool restoreFeed(const uint8_t *data, size_t len);
// Ends the restore (frees everything, removes the temporary file) and reports what happened.
// Safe to call after a failed restoreFeed() and when restoreBegin() was never called.
Result restoreFinish();
// True while a restore is in progress.
bool restoreActive();

}  // namespace backup
