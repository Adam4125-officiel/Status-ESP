// Full file-system backup and restore as a tar stream. See backup.h for the contract.
#include "backup.h"

#include <ArduinoJson.h>
#include <ESP8266WiFi.h>
#include <LittleFS.h>
#include <time.h>

#include <new>

#include "config.h"
#include "settings.h"

namespace backup {

const char MANIFEST_ENTRY[] = "status-esp-backup.json";
const char WIFI_ENTRY[] = "status-esp-wifi.json";

namespace {

const size_t BLOCK = 512;                 // tar works in 512-byte blocks
const size_t CHUNK = 1024;                // file data is streamed in pieces of this size
const size_t MAX_ENTRY_NAME = 99;         // a ustar name field is 100 bytes; keep the NUL
const uint32_t MAX_WIFI_ENTRY = 1024;
const uint8_t MAX_DEPTH = 4;
const char RESTORE_TMP[] = "/restore.tmp";

// Files that exist only while we are writing something else: never archived, never restored.
bool isTransient(const char *path) {
  return strcmp(path, config::SETTINGS_TMP) == 0 || strcmp(path, RESTORE_TMP) == 0;
}

size_t padded(size_t size) { return (size + BLOCK - 1) / BLOCK * BLOCK; }

// ---- tar header fields ----

// Octal text of `v`, `width` characters including the closing NUL (what tar expects).
void putOctal(char *dst, size_t width, uint32_t v) {
  dst[width - 1] = '\0';
  for (size_t i = width - 1; i > 0; i--) {
    dst[i - 1] = (char)('0' + (v & 7));
    v >>= 3;
  }
}

// Octal number in a header field: digits, then NULs or spaces only. False when it is anything
// else or does not fit in 31 bits (a file here is never anywhere near 2 GB).
bool parseOctal(const uint8_t *src, size_t width, uint32_t &out) {
  size_t i = 0;
  while (i < width && src[i] == ' ') i++;
  uint32_t v = 0;
  size_t digits = 0;
  for (; i < width && src[i] >= '0' && src[i] <= '7'; i++, digits++) {
    if (v > (0x7FFFFFFFu >> 3)) return false;
    v = (v << 3) | (uint32_t)(src[i] - '0');
  }
  if (digits == 0) return false;
  for (; i < width; i++) {
    if (src[i] != 0 && src[i] != ' ') return false;
  }
  out = v;
  return true;
}

// The tar checksum: the sum of every byte of the header, the checksum field counted as spaces.
uint32_t headerSum(const uint8_t *b) {
  uint32_t sum = 0;
  for (size_t i = 0; i < BLOCK; i++) sum += (i >= 148 && i < 156) ? ' ' : b[i];
  return sum;
}

// A ustar header for a regular file. `name` has no leading slash and at most 99 characters.
void makeHeader(uint8_t *b, const char *name, uint32_t size, uint32_t mtime) {
  memset(b, 0, BLOCK);
  strncpy((char *)b, name, 100);
  memcpy(b + 100, "0000644", 7);                // mode (the NUL is already there)
  memcpy(b + 108, "0000000", 7);                // uid
  memcpy(b + 116, "0000000", 7);                // gid
  putOctal((char *)b + 124, 12, size);
  putOctal((char *)b + 136, 12, mtime);
  b[156] = '0';                                 // regular file
  memcpy(b + 257, "ustar", 6);                  // magic, with its NUL
  memcpy(b + 263, "00", 2);                     // version
  putOctal((char *)b + 148, 7, headerSum(b));   // six digits and a NUL ...
  b[155] = ' ';                                 // ... then a space
}

// ---- the Wi-Fi network stored in the SDK's own flash area ----

// {"ssid":"...","pass":"..."}, or "" when the SDK holds no network. Read from the SDK's saved
// (default) station configuration, which is what WiFi.begin() with no arguments connects with.
String wifiJson() {
  struct station_config conf;
  memset(&conf, 0, sizeof(conf));
  if (!wifi_station_get_config_default(&conf) || conf.ssid[0] == '\0') return String();
  char ssid[33], pass[65];
  memcpy(ssid, conf.ssid, 32);
  ssid[32] = '\0';
  memcpy(pass, conf.password, 64);
  pass[64] = '\0';
  JsonDocument doc;
  doc["ssid"] = String(ssid);
  doc["pass"] = String(pass);
  String out;
  serializeJson(doc, out);
  return out;
}

// The first entry of every archive. A restore refuses anything that does not start with it, so
// that picking the wrong tar file (or any other file) cannot scatter its contents over the
// device's storage.
String manifestJson() { return String(F("{\"app\":\"" FW_NAME "\",\"format\":1,\"fw\":\"" FW_FULL_NAME "\"}")); }

// ---- walking the file system ----

// Called with each regular file: its path (leading slash) and size.
typedef void (*Visit)(void *ctx, const String &path, uint32_t size);

void walk(const String &dir, uint8_t depth, Visit fn, void *ctx) {
  Dir d = LittleFS.openDir(dir);
  while (d.next()) {
    String path = (dir.length() > 1 ? dir : String()) + "/" + d.fileName();
    if (d.isDirectory()) {
      if (depth < MAX_DEPTH) walk(path, depth + 1, fn, ctx);
      continue;
    }
    if (path.length() - 1 > MAX_ENTRY_NAME || isTransient(path.c_str())) continue;
    fn(ctx, path, (uint32_t)d.fileSize());
  }
}

void addToSize(void *ctx, const String &, uint32_t size) { *(size_t *)ctx += BLOCK + padded(size); }

struct WriteCtx {
  Sink sink;
  void *sinkCtx;
  uint8_t *buf;     // CHUNK bytes
  uint32_t mtime;
  bool ok;
};

void put(WriteCtx &w, const uint8_t *data, size_t len) {
  if (w.ok && !w.sink(w.sinkCtx, data, len)) w.ok = false;
}

void putPadding(WriteCtx &w, size_t size) {
  size_t pad = padded(size) - size;
  if (pad == 0) return;
  memset(w.buf, 0, pad);
  put(w, w.buf, pad);
}

void putFile(void *ctx, const String &path, uint32_t size) {
  WriteCtx &w = *(WriteCtx *)ctx;
  if (!w.ok) return;
  makeHeader(w.buf, path.c_str() + 1, size, w.mtime);
  put(w, w.buf, BLOCK);
  File f = LittleFS.open(path, "r");
  uint32_t left = size;
  while (left && w.ok) {
    size_t want = left < CHUNK ? left : CHUNK;
    size_t got = f ? f.read(w.buf, want) : 0;
    if (got < want) memset(w.buf + got, 0, want - got);   // the file shrank: keep the size promised
    put(w, w.buf, want);
    left -= want;
    yield();
  }
  if (f) f.close();
  putPadding(w, size);
}

// An entry that is not a file: `text` as the content of `name` (skipped when empty).
void putVirtual(WriteCtx &w, const char *name, const String &text) {
  if (!text.length()) return;
  makeHeader(w.buf, name, (uint32_t)text.length(), w.mtime);
  put(w, w.buf, BLOCK);
  for (size_t off = 0; off < text.length() && w.ok; off += CHUNK) {
    size_t n = text.length() - off < CHUNK ? text.length() - off : CHUNK;
    memcpy(w.buf, text.c_str() + off, n);
    put(w, w.buf, n);
  }
  putPadding(w, text.length());
}

}  // namespace

size_t tarSize() {
  size_t total = 2 * BLOCK;   // the end-of-archive marker
  total += BLOCK + padded(manifestJson().length());
  String wifi = wifiJson();
  if (wifi.length()) total += BLOCK + padded(wifi.length());
  walk("/", 0, addToSize, &total);
  return total;
}

bool writeTar(Sink sink, void *ctx) {
  if (ESP.getMaxFreeBlockSize() < CHUNK + 2048) return false;
  uint8_t *buf = new (std::nothrow) uint8_t[CHUNK];
  if (!buf) return false;

  WriteCtx w = {sink, ctx, buf, 0, true};
  time_t now = time(nullptr);
  if (now > 1600000000) w.mtime = (uint32_t)now;   // only once the clock has been set by NTP

  putVirtual(w, MANIFEST_ENTRY, manifestJson());
  putVirtual(w, WIFI_ENTRY, wifiJson());
  walk("/", 0, putFile, &w);
  memset(buf, 0, CHUNK);
  put(w, buf, 2 * BLOCK);

  bool ok = w.ok;
  delete[] buf;
  return ok;
}

// ============================ Restore ============================

namespace {

enum Phase : uint8_t { HEADER, DATA, PAD, DONE };
enum Kind : uint8_t { K_SKIP, K_FILE, K_WIFI };

struct Restore {
  uint8_t hdr[BLOCK];
  uint16_t fill;            // bytes of the current header collected so far
  Phase phase;
  Kind kind;
  bool seenManifest;        // the archive started with MANIFEST_ENTRY
  uint32_t remaining;       // data bytes of the current entry still to come
  uint16_t pad;             // padding bytes after the data
  bool tmpOpen;             // RESTORE_TMP is open for writing
  File file;
  char path[MAX_ENTRY_NAME + 2];   // destination of the current file, with its leading slash
  String wifiText;          // the virtual Wi-Fi entry, collected whole (at most MAX_WIFI_ENTRY bytes)
  Result res;
};

Restore *rs = nullptr;

bool fatal(const char *message) {
  strncpy(rs->res.error, message, sizeof(rs->res.error) - 1);
  rs->res.error[sizeof(rs->res.error) - 1] = '\0';
  rs->res.ok = false;
  return false;
}

// "./a/b", "/a/b" or "a/b" -> "/a/b". False for anything that is not a clean relative path:
// "..", an empty component ("a//b"), a backslash, a control character, a component longer than
// LittleFS allows, or a name too long for the archive format.
bool cleanName(const char *in, char *out, size_t cap) {
  while (in[0] == '/' || (in[0] == '.' && in[1] == '/')) in += in[0] == '/' ? 1 : 2;
  size_t n = 0, comp = 0;
  out[n++] = '/';
  for (;; in++) {
    char c = *in;
    if (c == '/' || c == '\0') {
      const char *start = out + n - comp;
      if (comp == 0 || (comp == 1 && start[0] == '.') || (comp == 2 && start[0] == '.' && start[1] == '.')) return false;
      if (c == '\0') break;
      comp = 0;
    } else {
      if ((uint8_t)c < 0x20 || c == 0x7F || c == '\\' || ++comp > config::MAX_FILE_NAME) return false;
    }
    if (n + 1 >= cap) return false;
    out[n++] = c;
  }
  out[n] = '\0';
  return n - 1 <= MAX_ENTRY_NAME;
}

// Creates the folders above `path` (LittleFS.rename() does not).
void makeParents(char *path) {
  for (char *p = strchr(path + 1, '/'); p; p = strchr(p + 1, '/')) {
    *p = '\0';
    LittleFS.mkdir(path);
    *p = '/';
  }
}

bool fitsInFreeSpace(uint32_t size) {
  FSInfo info;
  if (!LittleFS.info(info) || info.totalBytes <= info.usedBytes) return false;
  size_t freeBytes = info.totalBytes - info.usedBytes;
  return freeBytes > config::FS_RESERVE_BYTES && size <= freeBytes - config::FS_RESERVE_BYTES;
}

bool endData();

// The header block in rs->hdr is complete: decide what to do with the entry that follows.
bool startEntry() {
  const uint8_t *h = rs->hdr;
  bool allZero = true;
  for (size_t i = 0; i < BLOCK && allZero; i++) allZero = h[i] == 0;
  if (allZero) {                      // end-of-archive marker (one block is enough for us)
    rs->phase = DONE;
    return true;
  }
  uint32_t stored, size;
  if (!parseOctal(h + 148, 8, stored) || stored != headerSum(h)) return fatal("Not a tar archive (bad header)");
  if (!parseOctal(h + 124, 12, size)) return fatal("Unsupported entry size in the archive");

  rs->kind = K_SKIP;
  rs->remaining = size;
  rs->pad = (uint16_t)(padded(size) - size);

  // Only plain files with a plain name (no ustar "prefix" part) are ever restored.
  bool plain = (h[156] == '0' || h[156] == '\0') && h[345] == '\0';
  char raw[101], clean[MAX_ENTRY_NAME + 2];
  memcpy(raw, h, 100);
  raw[100] = '\0';
  bool named = plain && cleanName(raw, clean, sizeof(clean));

  if (!rs->seenManifest) {
    // The first entry must be ours: nothing is written for any other kind of archive.
    if (!named || strcmp(clean + 1, MANIFEST_ENTRY) != 0) return fatal("This is not a Status-ESP backup");
    rs->seenManifest = true;           // nothing to restore from it: its data is skipped below
  } else if (!named || isTransient(clean)) {
    rs->res.skipped++;
  } else if (strcmp(clean + 1, WIFI_ENTRY) == 0) {
    if (size <= MAX_WIFI_ENTRY) {
      rs->kind = K_WIFI;
      rs->wifiText = "";
    } else {
      rs->res.skipped++;
    }
  } else if (!fitsInFreeSpace(size)) {
    rs->res.skipped++;                 // does not fit: left alone, the rest is still restored
  } else {
    LittleFS.remove(RESTORE_TMP);      // a leftover of an earlier interrupted restore
    rs->file = LittleFS.open(RESTORE_TMP, "w");
    if (!rs->file) return fatal("Could not create a file: storage problem");
    rs->tmpOpen = true;
    strcpy(rs->path, clean);
    rs->kind = K_FILE;
  }
  rs->phase = size ? DATA : (rs->pad ? PAD : HEADER);
  if (size == 0 && rs->kind != K_SKIP) return endData();   // an empty file: nothing will follow
  return true;
}

// The data of the current entry is complete.
bool endData() {
  if (rs->kind == K_FILE) {
    rs->file.close();
    rs->tmpOpen = false;
    makeParents(rs->path);
    if (!LittleFS.rename(RESTORE_TMP, rs->path)) {
      LittleFS.remove(rs->path);       // some versions refuse to rename over a file
      if (!LittleFS.rename(RESTORE_TMP, rs->path)) {
        LittleFS.remove(RESTORE_TMP);
        return fatal("Could not write a file: storage problem");
      }
    }
    rs->res.restored++;
  } else if (rs->kind == K_WIFI) {
    JsonDocument doc;
    const char *ssid = nullptr, *pass = nullptr;
    const String &text = rs->wifiText;   // a const String: the instantiation web.cpp already has
    if (!deserializeJson(doc, text)) {
      ssid = doc["ssid"] | (const char *)nullptr;
      pass = doc["pass"] | "";
    }
    if (ssid && ssid[0] && strlen(ssid) <= 32 && strlen(pass) <= 63) {
      strcpy(rs->res.ssid, ssid);
      strcpy(rs->res.pass, pass);
      rs->res.haveWifi = true;
    } else {
      rs->res.skipped++;
    }
    rs->wifiText = "";
  }
  rs->kind = K_SKIP;
  return true;
}

}  // namespace

bool restoreBegin() {
  restoreFinish();   // never two at once
  if (!settings::fsMounted() || ESP.getMaxFreeBlockSize() < sizeof(Restore) + 4096) return false;
  rs = new (std::nothrow) Restore();
  if (!rs) return false;
  rs->phase = HEADER;
  rs->kind = K_SKIP;
  rs->seenManifest = false;
  memset(&rs->res, 0, sizeof(rs->res));
  rs->res.ok = true;
  return true;
}

bool restoreFeed(const uint8_t *data, size_t len) {
  if (!rs || !rs->res.ok) return false;
  while (len) {
    switch (rs->phase) {
      case HEADER: {
        size_t n = BLOCK - rs->fill < len ? BLOCK - rs->fill : len;
        memcpy(rs->hdr + rs->fill, data, n);
        rs->fill += (uint16_t)n;
        data += n;
        len -= n;
        if (rs->fill == BLOCK) {
          rs->fill = 0;
          if (!startEntry()) return false;
        }
        break;
      }
      case DATA: {
        size_t n = rs->remaining < len ? rs->remaining : len;
        if (rs->kind == K_FILE) {
          if (rs->file.write(data, n) != n) {
            rs->file.close();
            rs->tmpOpen = false;
            LittleFS.remove(RESTORE_TMP);
            return fatal("Storage is full: the restore was stopped");
          }
        } else if (rs->kind == K_WIFI) {
          rs->wifiText.concat((const char *)data, (unsigned int)n);
        }
        data += n;
        len -= n;
        rs->remaining -= (uint32_t)n;
        if (rs->remaining == 0) {
          if (!endData()) return false;
          rs->phase = rs->pad ? PAD : HEADER;
        }
        break;
      }
      case PAD: {
        size_t n = rs->pad < len ? rs->pad : len;
        rs->pad -= (uint16_t)n;
        data += n;
        len -= n;
        if (rs->pad == 0) rs->phase = HEADER;
        break;
      }
      case DONE:
        return true;   // anything after the end marker is ignored
    }
    yield();
  }
  return true;
}

Result restoreFinish() {
  Result r;
  memset(&r, 0, sizeof(r));
  if (!rs) {
    r.ok = true;
    return r;
  }
  if (rs->res.ok && !rs->seenManifest) fatal("This is not a Status-ESP backup");
  if (rs->res.ok && !(rs->phase == DONE || (rs->phase == HEADER && rs->fill == 0))) fatal("The archive is cut short (incomplete upload)");
  if (rs->tmpOpen) rs->file.close();
  if (rs->tmpOpen || rs->phase == DATA) LittleFS.remove(RESTORE_TMP);
  r = rs->res;
  delete rs;
  rs = nullptr;
  return r;
}

bool restoreActive() { return rs != nullptr; }

}  // namespace backup
