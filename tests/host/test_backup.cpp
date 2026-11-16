// Host test of the restore side of src/backup.cpp (restoreBegin / restoreFeed / restoreFinish) and of the writer
// that makes the archives it reads, against an in-memory LittleFS (tests/host/backup/). backup.cpp is built
// unchanged. Built and run by tools/test_host.sh with the sanitizers on.
//
// Whatever the archive holds, a restore may only (CLAUDE.md rule 2): create /restore.tmp, rename it over a clean
// relative path, create the folders above that path, and remove /restore.tmp or a path it is about to replace.
// Never format, never delete anything else. check() asserts that after every run, including the fuzzer's.
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <algorithm>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "LittleFS.h"
#include "ESP8266WiFi.h"
#include "backup.h"
#include "settings.h"

// ---- the stand-ins' state ----
FakeFs fakeFs;
LittleFSClass LittleFS;
EspClass ESP;
station_config fakeWifiConfig;
bool fakeWifiPresent = false;
namespace settings {
bool fsMounted() { return true; }
}

static int failures = 0, checks = 0;
#define CHECK(cond)                                                \
  do {                                                             \
    checks++;                                                      \
    if (!(cond)) {                                                 \
      failures++;                                                  \
      printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);       \
    }                                                              \
  } while (0)

typedef std::vector<uint8_t> Bytes;
static const char TMP[] = "/restore.tmp";

// ---- tar builder ----
struct Entry {
  std::string name, data;
  char type = '0';
  std::string sizeField;   // when not empty, written instead of the real size (12 bytes, as given)
  std::string prefix;
  bool badSum = false;
  bool noPad = false;
};

static void octal(uint8_t *dst, size_t width, uint32_t v) {
  dst[width - 1] = 0;
  for (size_t i = width - 1; i > 0; i--) { dst[i - 1] = (uint8_t)('0' + (v & 7)); v >>= 3; }
}

static uint32_t sumOf(const uint8_t *b) {
  uint32_t s = 0;
  for (size_t i = 0; i < 512; i++) s += (i >= 148 && i < 156) ? ' ' : b[i];
  return s;
}

static void fixSum(uint8_t *b) {
  octal(b + 148, 7, sumOf(b));
  b[155] = ' ';
}

static void addEntry(Bytes &t, const Entry &e) {
  uint8_t h[512];
  memset(h, 0, sizeof h);
  memcpy(h, e.name.data(), std::min<size_t>(e.name.size(), 100));
  memcpy(h + 100, "0000644", 7);
  octal(h + 124, 12, (uint32_t)e.data.size());
  if (!e.sizeField.empty()) { memset(h + 124, 0, 12); memcpy(h + 124, e.sizeField.data(), std::min<size_t>(e.sizeField.size(), 12)); }
  h[156] = (uint8_t)e.type;
  memcpy(h + 257, "ustar", 6);
  memcpy(h + 345, e.prefix.data(), std::min<size_t>(e.prefix.size(), 155));
  fixSum(h);
  if (e.badSum) h[148] = (h[148] == '1') ? '2' : '1';
  t.insert(t.end(), h, h + 512);
  t.insert(t.end(), e.data.begin(), e.data.end());
  if (!e.noPad) t.resize((t.size() + 511) / 512 * 512);
}

static void addEnd(Bytes &t) { t.resize(t.size() + 1024, 0); }

static Entry manifest() { Entry e; e.name = "status-esp-backup.json"; e.data = "{\"app\":\"Status-ESP\",\"format\":1}"; return e; }

static Bytes tarOf(const std::vector<Entry> &es, bool marker = true, bool end = true) {
  Bytes t;
  if (marker) addEntry(t, manifest());
  for (auto &e : es) addEntry(t, e);
  if (end) addEnd(t);
  return t;
}

static Entry file(const std::string &name, const std::string &data) { Entry e; e.name = name; e.data = data; return e; }

// ---- running a restore ----
struct Outcome {
  backup::Result r;
  bool feedOk;
  std::map<std::string, std::string> files;
};

static bool sameResult(const backup::Result &a, const backup::Result &b) {
  return a.ok == b.ok && a.restored == b.restored && a.skipped == b.skipped && a.haveWifi == b.haveWifi &&
         strcmp(a.ssid, b.ssid) == 0 && strcmp(a.pass, b.pass) == 0 && strcmp(a.error, b.error) == 0;
}

static bool sane(const std::string &p, bool allowTmp) {
  if (p.empty() || p[0] != '/' || p.size() > 100 || p.back() == '/') return false;
  if (!allowTmp && (p == "/custom.tmp" || p == TMP)) return false;
  size_t i = 1;
  while (i <= p.size()) {
    size_t j = p.find('/', i);
    if (j == std::string::npos) j = p.size();
    std::string c = p.substr(i, j - i);
    if (c.empty() || c == "." || c == ".." || c.size() > 31) return false;
    for (char ch : c) if ((uint8_t)ch < 0x20 || ch == 0x7F || ch == '\\') return false;
    i = j + 1;
  }
  return true;
}

// Rule 2: what the run did to the file system, whatever the archive said.
static void check(const std::map<std::string, std::string> &before, const char *what) {
  bool ok = true;
  if (fakeFs.formats != 0) ok = false;
  for (auto &c : fakeFs.creates) if (c != TMP) ok = false;
  std::set<std::string> dests;
  for (auto &r : fakeFs.renames) {
    if (r.first != TMP || !sane(r.second, false)) ok = false;
    dests.insert(r.second);
  }
  for (auto &d : fakeFs.mkdirs) if (!sane(d, false)) ok = false;
  for (auto &r : fakeFs.removes) if (r != TMP && !dests.count(r)) ok = false;
  if (fakeFs.files.count(TMP)) ok = false;   // the temporary file never outlives a finished restore
  for (auto &f : before) {   // nothing that was there is gone, unless a restored file replaced it
    auto it = fakeFs.files.find(f.first);
    if (it == fakeFs.files.end() && f.first != TMP) ok = false;
    if (it != fakeFs.files.end() && it->second != f.second && !dests.count(f.first)) ok = false;
  }
  for (auto &f : fakeFs.files) if (!before.count(f.first) && !dests.count(f.first) && f.first != TMP) ok = false;
  checks++;
  if (!ok) { failures++; printf("FAIL rule 2 (%s)\n", what); }
}

static Outcome restore(const Bytes &tar, size_t chunk, const std::map<std::string, std::string> &initial = {},
                       std::function<void()> tweak = nullptr) {
  fakeFs = FakeFs();
  fakeFs.files = initial;
  if (tweak) tweak();
  Outcome o;
  memset(&o.r, 0, sizeof o.r);
  CHECK(backup::restoreBegin());
  o.feedOk = true;
  size_t off = 0;
  if (tar.empty()) o.feedOk = backup::restoreFeed(nullptr, 0);
  while (off < tar.size() && o.feedOk) {
    size_t n = std::min(chunk, tar.size() - off);
    uint8_t *exact = new uint8_t[n];   // exactly sized: the sanitizer sees any read past it
    memcpy(exact, tar.data() + off, n);
    o.feedOk = backup::restoreFeed(exact, n);
    delete[] exact;
    off += n;
  }
  o.r = backup::restoreFinish();
  o.files = fakeFs.files;
  check(initial, "restore");
  return o;
}

static Outcome restore(const Bytes &tar) { return restore(tar, 1460); }

static const size_t CHUNKS[] = {1, 7, 512, 513, 1460};

// The same archive in every chunk size must give the same result and the same files.
static Outcome restoreAllChunkings(const Bytes &tar, const std::map<std::string, std::string> &initial = {}) {
  Outcome first = restore(tar, 1460, initial);
  for (size_t c : CHUNKS) {
    Outcome o = restore(tar, c, initial);
    CHECK(sameResult(o.r, first.r));
    CHECK(o.files == first.files);
    CHECK(o.feedOk == first.feedOk);
  }
  return first;
}

// ---- tests ----

static std::map<std::string, std::string> populate() {
  std::map<std::string, std::string> f;
  f["/custom.json"] = "{\"brt\":50}";
  f["/config.json"] = "{\"s\":\"net\",\"p\":\"secret\"}";
  f["/image/boot.jpg"] = std::string(1500, 'J');
  f["/image/a.jpg"] = std::string(512, 'a');
  f["/image/b.jpg"] = std::string(513, 'b');
  f["/gif/anim.gif"] = std::string(2049, 'G');
  f["/font/f.vlw"] = std::string(1000, 'v');
  f["/empty.txt"] = "";
  f["/deep/er/than/that/x.txt"] = "x";
  f["/custom.tmp"] = "transient";
  return f;
}

static Bytes writeArchive() {
  Bytes out;
  backup::writeTar([](void *ctx, const uint8_t *d, size_t n) { ((Bytes *)ctx)->insert(((Bytes *)ctx)->end(), d, d + n); return true; }, &out);
  return out;
}

static Bytes roundTripTar;
static std::vector<size_t> roundTripBoundaries;   // offsets where one entry ends and the next begins

static void roundTrip() {
  fakeFs = FakeFs();
  fakeFs.files = populate();
  fakeWifiPresent = true;
  memset(&fakeWifiConfig, 0, sizeof fakeWifiConfig);
  strcpy((char *)fakeWifiConfig.ssid, "HomeNet");
  strcpy((char *)fakeWifiConfig.password, "pa\"ss");
  size_t promised = backup::tarSize();
  roundTripTar = writeArchive();
  CHECK(roundTripTar.size() == promised);
  CHECK(roundTripTar.size() % 512 == 0);
  // The writer never archives the transient file and starts with the marker.
  CHECK(memcmp(roundTripTar.data(), "status-esp-backup.json", 22) == 0);
  CHECK(std::string((const char *)roundTripTar.data(), roundTripTar.size()).find("custom.tmp") == std::string::npos);

  // Entry boundaries, read from the archive itself.
  for (size_t off = 0; off + 512 <= roundTripTar.size();) {
    roundTripBoundaries.push_back(off);
    uint32_t size = (uint32_t)strtoul((const char *)&roundTripTar[off + 124], nullptr, 8);
    if (roundTripTar[off] == 0) break;
    off += 512 + (size + 511) / 512 * 512;
  }

  auto expected = populate();
  expected.erase("/custom.tmp");
  std::map<std::string, std::string> empty;
  Outcome o = restoreAllChunkings(roundTripTar, empty);
  CHECK(o.feedOk && o.r.ok);
  CHECK(o.r.restored == expected.size());
  CHECK(o.r.skipped == 0);
  CHECK(o.files == expected);
  CHECK(o.r.haveWifi && strcmp(o.r.ssid, "HomeNet") == 0 && strcmp(o.r.pass, "pa\"ss") == 0);

  // Restoring over a device that already has newer files replaces them (and only them).
  auto device = populate();
  device["/custom.json"] = "{\"brt\":1}";
  device["/image/mine.jpg"] = "mine";
  o = restoreAllChunkings(roundTripTar, device);
  CHECK(o.r.ok && o.files["/custom.json"] == "{\"brt\":50}" && o.files["/image/mine.jpg"] == "mine");

  // No Wi-Fi network in the SDK: no wifi entry, no Wi-Fi in the result.
  fakeWifiPresent = false;
  fakeFs = FakeFs();
  fakeFs.files = populate();
  Bytes noWifi = writeArchive();
  CHECK(noWifi.size() == roundTripTar.size() - 512 - 512);
  o = restore(noWifi);
  CHECK(o.r.ok && !o.r.haveWifi);
  fakeWifiPresent = true;
}

static void refusals() {
  // No marker: refused before anything is written, whatever else the archive holds.
  std::vector<std::vector<Entry>> others = {
      {file("custom.json", "{}")}, {file("../x", "x")}, {file("status-esp-wifi.json", "{\"ssid\":\"a\"}")}, {}};
  for (auto &es : others) {
    Outcome o = restoreAllChunkings(tarOf(es, false), {{"/keep.txt", "k"}});
    CHECK(!o.r.ok && o.r.restored == 0 && !o.r.haveWifi);
    CHECK(strstr(o.r.error, "not a Status-ESP backup") != nullptr);
    CHECK(o.files.size() == 1);
  }
  // The marker under another name or in a folder is not the marker.
  Outcome o = restore(tarOf({}, false, true));
  CHECK(!o.r.ok);
  Entry m = manifest();
  m.name = "sub/status-esp-backup.json";
  Bytes t;
  addEntry(t, m);
  addEnd(t);
  CHECK(!restore(t).r.ok);
  m.name = "STATUS-ESP-BACKUP.JSON";
  t.clear();
  addEntry(t, m);
  addEnd(t);
  CHECK(!restore(t).r.ok);
  // Empty upload, a text file, random bytes.
  CHECK(!restore(Bytes()).r.ok);
  Bytes text(5000, 'x');
  o = restore(text);
  CHECK(!o.r.ok && !o.feedOk);
  // Only the marker and the end: fine, nothing restored.
  o = restore(tarOf({}));
  CHECK(o.r.ok && o.r.restored == 0);
  // Marker alone, no end marker: ends at a block boundary, accepted (see the report).
  o = restore(tarOf({}, true, false));
  CHECK(o.r.ok && o.r.restored == 0);
  // After the end marker everything is ignored, garbage included.
  t = tarOf({file("a.txt", "A")});
  t.insert(t.end(), 3000, 0xAB);
  o = restoreAllChunkings(t);
  CHECK(o.r.ok && o.files.size() == 1 && o.files["/a.txt"] == "A");
  // A feed after a fatal error, a feed with no restore, two begins, finish without begin.
  CHECK(!backup::restoreFeed((const uint8_t *)"x", 1));
  backup::Result r = backup::restoreFinish();
  CHECK(r.ok && r.restored == 0);
  CHECK(backup::restoreBegin());
  CHECK(backup::restoreBegin());   // the first one is dropped
  CHECK(backup::restoreActive());
  r = backup::restoreFinish();
  CHECK(!r.ok && !backup::restoreActive());   // nothing was fed: not a backup
}

static void names() {
  struct Case { std::string name; std::string path; };   // path empty: skipped
  std::vector<Case> cases = {
      {"ok.txt", "/ok.txt"},
      {"./ok2.txt", "/ok2.txt"},
      {"/abs.txt", "/abs.txt"},
      {"//double.txt", "/double.txt"},
      {"a/b/c.txt", "/a/b/c.txt"},
      {"../evil.txt", ""},
      {"a/../evil.txt", ""},
      {"a/..", ""},
      {"..", ""},
      {".", ""},
      {"a/./b.txt", ""},
      {"a//b.txt", ""},
      {"dir/", ""},
      {"", ""},
      {"back\\slash.txt", ""},
      {std::string("ctl\x01.txt"), ""},
      {std::string("del\x7f.txt"), ""},
      {std::string(31, 'c'), "/" + std::string(31, 'c')},
      {std::string(32, 'c'), ""},
      {"d/" + std::string(32, 'c'), ""},
      {std::string(30, 'p') + "/" + std::string(30, 'q') + "/" + std::string(30, 'r'), "/" + std::string(30, 'p') + "/" + std::string(30, 'q') + "/" + std::string(30, 'r')},
      {"custom.tmp", ""},
      {"restore.tmp", ""},
      {"/restore.tmp", ""},
      {"./custom.tmp", ""},
      {"x.json", "/x.json"},
  };
  // 99 characters fits, 100 does not (a ustar name field has no NUL room here).
  std::string n99 = std::string(31, 'a') + "/" + std::string(31, 'b') + "/" + std::string(31, 'c') + "/ddd";   // 99
  std::string n100 = n99 + "d";
  cases.push_back({n99, "/" + n99});
  cases.push_back({n100, ""});
  std::vector<Entry> es;
  std::map<std::string, std::string> want;
  for (auto &c : cases) {
    es.push_back(file(c.name, "data:" + c.name));
    if (!c.path.empty()) want[c.path] = "data:" + c.name;
  }
  Outcome o = restoreAllChunkings(tarOf(es));
  CHECK(o.r.ok);
  if (o.files != want) {
    for (auto &w : want) if (!o.files.count(w.first)) printf("  missing %s\n", w.first.c_str());
    for (auto &f : o.files) if (!want.count(f.first)) printf("  unexpected %s\n", f.first.c_str());
  }
  CHECK(o.files == want);
  CHECK(o.r.restored == want.size());
  CHECK(o.r.skipped == cases.size() - want.size());

  // A second marker is an ordinary entry; the Wi-Fi entry is never a file.
  o = restore(tarOf({file("status-esp-wifi.json", "{\"ssid\":\"n\",\"pass\":\"p\"}")}));
  CHECK(o.r.ok && o.r.haveWifi && o.files.empty());
}

static void headers() {
  // Bad checksums, bad size fields, odd types.
  Entry bad = file("a.txt", "A");
  bad.badSum = true;
  Outcome o = restoreAllChunkings(tarOf({file("first.txt", "1"), bad, file("later.txt", "2")}));
  CHECK(!o.r.ok && o.files.count("/first.txt") && !o.files.count("/later.txt") && !o.feedOk);

  Bytes t = tarOf({file("a.txt", "A")});
  t[1024 + 150] ^= 0x01;   // the checksum of a.txt's header (the marker takes blocks 0 and 1)
  o = restore(t);
  CHECK(!o.r.ok && o.files.empty());
  t = tarOf({file("a.txt", "A")});
  t[512 + 3] ^= 0x01;      // the marker's data is not checked: it is skipped
  CHECK(restore(t).r.ok);
  t = tarOf({file("a.txt", "A")});
  t[148] = 'z';        // the marker's own checksum field is not octal
  CHECK(!restore(t).r.ok);
  t = tarOf({file("a.txt", "A")});
  t[0] = 'X';          // the marker's name changed after the checksum was made
  CHECK(!restore(t).r.ok);

  for (const char *sf : {"abcdefghijk", "77777777777", "99999999999", "-1", "1 2", "0x10", "7777777777777", "            "}) {
    Entry e = file("a.txt", "A");
    e.sizeField = sf;
    o = restoreAllChunkings(tarOf({e, file("b.txt", "B")}));
    CHECK(!o.r.ok || o.files.count("/a.txt") == 0 || o.files["/a.txt"].size() <= 1);
    CHECK(o.r.restored <= 2);
  }
  { Entry e = file("a.txt", "A"); e.sizeField = "77777777777"; CHECK(!restore(tarOf({e})).r.ok); }
  { Entry e = file("a.txt", "A"); e.sizeField = "abcdefghijk"; CHECK(!restore(tarOf({e})).r.ok); }
  // Spaces around a valid number are what tar writers produce.
  { Entry e = file("a.txt", "AB"); e.sizeField = "       2 "; o = restore(tarOf({e})); CHECK(o.r.ok && o.files["/a.txt"] == "AB"); }

  // The size says more than the archive holds: cut short, no half file, no temporary file.
  { Entry e = file("a.txt", std::string(100, 'x')); e.sizeField = "00000100000"; e.noPad = true;
    o = restoreAllChunkings(tarOf({e}, true, false)); CHECK(!o.r.ok && o.files.empty()); }
  // The size says less than the data that follows: the surplus is read as headers and refused or ignored.
  { Entry e = file("a.txt", std::string(1200, 'x')); e.sizeField = "00000000005";
    o = restoreAllChunkings(tarOf({e})); CHECK(o.files.count("/a.txt") ? o.files["/a.txt"] == "xxxxx" : true); }
  // Folders, links, extended headers and ustar prefixes are skipped, their data too.
  for (char type : {'5', '2', '1', 'x', 'g', 'L', '7'}) {
    Entry e = file("odd.txt", "payload");
    e.type = type;
    o = restoreAllChunkings(tarOf({e, file("after.txt", "A")}));
    CHECK(o.r.ok && o.files.size() == 1 && o.files.count("/after.txt") && o.r.skipped == 1);
  }
  { Entry e = file("pre.txt", "payload"); e.prefix = "some/dir";
    o = restoreAllChunkings(tarOf({e, file("after.txt", "A")})); CHECK(o.r.ok && o.files.size() == 1 && o.r.skipped == 1); }
  { Entry e = file("nul.txt", "x"); e.type = '\0';   // old tar: a NUL type flag is a regular file
    o = restore(tarOf({e})); CHECK(o.r.ok && o.files.count("/nul.txt")); }
  // An empty file, and several in a row (no data blocks at all).
  o = restoreAllChunkings(tarOf({file("e1", ""), file("e2", ""), file("e3", "")}));
  CHECK(o.r.ok && o.r.restored == 3 && o.files.size() == 3);
  // Data of exactly 512 and 1024 bytes (no padding) and 511, 513.
  for (size_t n : {511u, 512u, 513u, 1023u, 1024u, 1025u}) {
    o = restoreAllChunkings(tarOf({file("n.bin", std::string(n, 'n')), file("m.bin", "M")}));
    CHECK(o.r.ok && o.files["/n.bin"].size() == n && o.files["/m.bin"] == "M");
  }
}

static void wifiEntries() {
  auto run = [](const std::string &json) { return restoreAllChunkings(tarOf({file("status-esp-wifi.json", json)})); };
  Outcome o = run("{\"ssid\":\"Net\",\"pass\":\"word\"}");
  CHECK(o.r.ok && o.r.haveWifi && strcmp(o.r.ssid, "Net") == 0 && strcmp(o.r.pass, "word") == 0 && o.r.skipped == 0);
  o = run("{\"ssid\":\"Open\"}");
  CHECK(o.r.haveWifi && strcmp(o.r.pass, "") == 0);
  for (const char *bad : {"", "{", "[]", "null", "{\"ssid\":\"\"}", "{\"ssid\":5}", "{\"pass\":\"x\"}"}) {
    o = run(bad);
    CHECK(o.r.ok && !o.r.haveWifi && o.r.skipped == 1);
  }
  o = run("{\"ssid\":\"" + std::string(32, 's') + "\",\"pass\":\"" + std::string(63, 'p') + "\"}");
  CHECK(o.r.haveWifi && strlen(o.r.ssid) == 32 && strlen(o.r.pass) == 63);
  o = run("{\"ssid\":\"" + std::string(33, 's') + "\"}");
  CHECK(!o.r.haveWifi && o.r.skipped == 1);
  o = run("{\"ssid\":\"a\",\"pass\":\"" + std::string(64, 'p') + "\"}");
  CHECK(!o.r.haveWifi && o.r.skipped == 1);
  // Over 1024 bytes: skipped without being collected.
  o = run("{\"ssid\":\"a\",\"x\":\"" + std::string(1100, 'x') + "\"}");
  CHECK(o.r.ok && !o.r.haveWifi && o.r.skipped == 1);
}

static void storage() {
  // Does not fit (reserve): skipped, the rest still restored.
  Bytes t = tarOf({file("big.bin", std::string(5000, 'b')), file("small.txt", "s")});
  Outcome o = restore(t, 1460, {}, [] { fakeFs.total = 16 * 1024 + 100; });
  CHECK(o.r.ok && o.files.size() == 1 && o.files.count("/small.txt") && o.r.skipped == 1);
  // No room at all.
  o = restore(t, 1460, {}, [] { fakeFs.total = 10; });
  CHECK(o.r.ok && o.files.empty() && o.r.skipped == 2);
  // The write comes up short in the middle of a file: stopped, no half file, tmp removed.
  o = restore(t, 1460, {}, [] { fakeFs.writeBudget = 1000; });
  CHECK(!o.r.ok && !o.feedOk && o.files.empty());
  // The rename fails: stopped, tmp removed.
  o = restore(t, 1460, {}, [] { fakeFs.renameFails = true; });
  CHECK(!o.r.ok && o.files.empty());
  // A leftover temporary file from an interrupted restore is replaced.
  o = restore(tarOf({file("a.txt", "A")}), 1460, {{TMP, "stale"}});
  CHECK(o.r.ok && o.files.count("/a.txt") && !o.files.count(TMP));
  // The destination exists and renaming over it works (a LittleFS that allows it).
  o = restore(tarOf({file("a.txt", "new")}), 1460, {{"/a.txt", "old"}}, [] { fakeFs.renameOverFails = false; });
  CHECK(o.r.ok && o.files["/a.txt"] == "new");
  o = restore(tarOf({file("a.txt", "new")}), 1460, {{"/a.txt", "old"}});
  CHECK(o.r.ok && o.files["/a.txt"] == "new");
}

static void truncations() {
  // Cut at every length: never a half file, never ok inside a block, ok exactly between entries.
  auto original = populate();
  original.erase("/custom.tmp");
  std::set<size_t> boundaries(roundTripBoundaries.begin(), roundTripBoundaries.end());
  size_t okCount = 0;
  for (size_t len = 0; len <= roundTripTar.size(); len++) {
    Bytes cut(roundTripTar.begin(), roundTripTar.begin() + len);
    Outcome o = restore(cut, 1460, {});
    bool atBoundary = boundaries.count(len) > 0 && len > 0;
    bool full = len >= roundTripTar.size() - 1024;   // the end marker's first block is enough
    bool afterFirstEndBlock = len >= roundTripTar.size() - 512;   // the DONE phase: the rest is ignored
    if (len % 512 != 0 && !afterFirstEndBlock) CHECK(!o.r.ok);
    if (len == roundTripTar.size()) CHECK(o.r.ok);
    if (o.r.ok) okCount++;
    CHECK(!o.r.ok || atBoundary || full || len % 512 == 0);
    for (auto &f : o.files) CHECK(original.count(f.first) && original[f.first] == f.second);   // only whole files
    CHECK(o.r.restored == o.files.size());
  }
  CHECK(okCount >= 2);
  // The same cut, fed in awkward chunks: same verdict at every block boundary.
  for (size_t len = 0; len <= roundTripTar.size(); len += 512) {
    Bytes cut(roundTripTar.begin(), roundTripTar.begin() + len);
    Outcome a = restore(cut, 1460), b = restore(cut, 7), c = restore(cut, 513);
    CHECK(sameResult(a.r, b.r) && sameResult(a.r, c.r) && a.files == b.files && a.files == c.files);
  }
}

// ---- fuzzer ----
static uint64_t rng = 0xBAC4C0DEULL;
static uint32_t rnd() {
  uint64_t z = (rng += 0x9E3779B97F4A7C15ULL);
  z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
  z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
  return (uint32_t)((z ^ (z >> 31)) >> 16);
}

// Walks the archive by its own size fields and rewrites each header's checksum, so that mutations get past the
// checksum and reach the logic behind it.
static void refix(Bytes &t) {
  size_t off = 0;
  while (off + 512 <= t.size()) {
    if (rnd() % 8 != 0) fixSum(&t[off]);
    char *end = nullptr;
    std::string f((const char *)&t[off + 124], 12);
    f.resize(strnlen(f.c_str(), 12));
    unsigned long size = strtoul(f.c_str(), &end, 8);
    if (size > 100000) break;
    off += 512 + (size + 511) / 512 * 512;
  }
}

static void mutate(Bytes &t) {
  unsigned muts = 1 + rnd() % 8;
  for (unsigned m = 0; m < muts && !t.empty(); m++) {
    switch (rnd() % 7) {
      case 0: t[rnd() % t.size()] = (uint8_t)rnd(); break;
      case 1: t[rnd() % t.size()] ^= (uint8_t)(1u << (rnd() % 8)); break;
      case 2: { size_t off = (rnd() % (t.size() / 512 + 1)) * 512; if (off + 512 <= t.size()) t[off + (rnd() % 256)] = (uint8_t)rnd(); break; }   // a header
      case 3: { size_t off = (rnd() % (t.size() / 512 + 1)) * 512; if (off + 136 <= t.size()) t[off + 124 + rnd() % 12] = (uint8_t)("01234567 \0z"[rnd() % 11]); break; }   // a size digit
      case 4: { size_t off = (rnd() % (t.size() / 512 + 1)) * 512; if (off + 100 <= t.size()) t[off + rnd() % 100] = (uint8_t)("./\\.a\x01"[rnd() % 6]); break; }   // a name byte
      case 5: if (rnd() % 4 == 0) t.resize(rnd() % (t.size() + 1)); break;
      case 6: if (rnd() % 4 == 0) { size_t at = (rnd() % (t.size() / 512 + 1)) * 512; t.insert(t.begin() + std::min(at, t.size()), 512, (uint8_t)(rnd() % 3 ? 0 : rnd())); } break;
    }
  }
  if (rnd() % 3) refix(t);
}

static void fuzz() {
  std::vector<Bytes> seeds;
  seeds.push_back(roundTripTar);
  std::vector<Entry> es;
  for (const std::string &n : std::vector<std::string>{"ok.txt", "../x", "a//b", "a/b/c.txt", "custom.tmp", "status-esp-wifi.json", "d/", "/abs", std::string(40, 'z')})
    es.push_back(file(n, n[0] == 's' ? "{\"ssid\":\"n\"}" : "data"));
  Entry e = file("odd", "zz"); e.type = '5'; es.push_back(e);
  es.push_back(file("big.bin", std::string(1500, 'B')));
  seeds.push_back(tarOf(es));
  const size_t total = 30000;
  size_t okRuns = 0, restoredFiles = 0;
  for (size_t it = 0; it < total; it++) {
    Bytes t = seeds[rnd() % seeds.size()];
    mutate(t);
    auto initial = populate();
    size_t totalSize = (rnd() % 5 == 0) ? 16 * 1024 + rnd() % 4000 : 1000000;
    size_t chunk = 1 + rnd() % 1500;
    Outcome o = restore(t, chunk, initial, [&] { fakeFs.total = totalSize; if (rnd() % 8 == 0) fakeFs.renameOverFails = false; });
    if (o.r.ok) okRuns++;
    restoredFiles += o.r.restored;
    CHECK(strnlen(o.r.error, sizeof o.r.error) < sizeof o.r.error);
    CHECK(strnlen(o.r.ssid, sizeof o.r.ssid) < sizeof o.r.ssid && strnlen(o.r.pass, sizeof o.r.pass) < sizeof o.r.pass);
    if (it % 8 == 0) {   // the same archive in other chunks: same outcome
      Outcome p = restore(t, 1 + rnd() % 1500, initial, [&] { fakeFs.total = totalSize; });
      Outcome q = restore(t, 1460, initial, [&] { fakeFs.total = totalSize; });
      CHECK(p.files == q.files);
      CHECK(sameResult(p.r, q.r));
    }
  }
  printf("backup fuzz: %zu of %zu mutated archives read to the end, %zu files restored\n", okRuns, total, restoredFiles);
}

int main() {
  setvbuf(stdout, nullptr, _IONBF, 0);
  roundTrip();
  refusals();
  names();
  headers();
  wifiEntries();
  storage();
  truncations();
  fuzz();
  printf("%d checks, %d failed\n", checks, failures);
  return failures ? 1 : 0;
}
