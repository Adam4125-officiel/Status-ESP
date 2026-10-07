// Theme "album": the pictures in /image (.jpg .jpeg .gif), one after the other.
//
//   album_auto on : every picture for album_interval seconds, in the file system's order,
//                   wrapping round. A GIF animates (looping) during its slot.
//   album_auto off: one picture stays: album_file, or the first one when that is unset or gone.
//                   A JPG is drawn once, a GIF animates until the theme changes.
//
// The folder is never held in memory: finding "the picture after this one" walks the
// directory (a few entries, once per picture change) and remembers only file names. A
// picture that cannot be shown (not a baseline JPEG, a damaged GIF, no memory for the GIF
// decoder) is skipped, one per pass; when every picture has failed in a row the reason is
// shown and the folder is tried again every few seconds.
//
// Update(false) does work only when a picture is due or a GIF frame is due, and a JPG decode
// (100-300 ms) happens once per picture, as display.h allows.
#include "config.h"
#include "display.h"
#include "media.h"
#include "settings.h"

namespace {

const uint32_t RETRY_MS = 5000;     // between attempts while nothing can be shown
const uint32_t RESCAN_MS = 1500;    // between looks at an empty folder

enum Phase : uint8_t {
  PH_PICK,       // choose and show a picture now
  PH_JPG,        // a JPG is on screen; waiting for the slot to end (auto) or for ever (manual)
  PH_GIF,        // a GIF is playing
  PH_NOTICE      // a message is on screen; look again at nextTry
};
enum Notice : int8_t { NOTICE_NONE = -1, NOTICE_EMPTY = 0, NOTICE_ERROR = 1 };

Phase phase;
char current[config::MAX_FILE_NAME + 1];   // the picture being shown ("" = none yet)
bool advance;                              // true: move on from `current`; false: show it again
bool onScreen;                             // `current` is really on the screen (and phase says how)
uint32_t slotStart;                        // millis() when the current picture appeared
uint32_t nextTry;                          // millis() before which PH_NOTICE does not look again
uint16_t failures;                         // pictures in a row that could not be shown
uint16_t lastCount;                        // pictures found by the last scan
int8_t shownNotice;

struct Scan {
  const char *current;        // file to find the successor of ("" = none)
  const char *wanted;         // album_file ("" = none)
  char first[config::MAX_FILE_NAME + 1];
  char next[config::MAX_FILE_NAME + 1];   // the file after `current`
  bool seenCurrent, wantedFound;
  uint16_t count;
};

void scanFile(const char *name, size_t size, void *ctx) {
  Scan *s = static_cast<Scan *>(ctx);
  if (size == 0 || strlen(name) > config::MAX_FILE_NAME) return;
  if (!media::isJpg(name) && !media::isGif(name)) return;
  s->count++;
  if (!s->first[0]) strlcpy(s->first, name, sizeof(s->first));
  if (s->seenCurrent && !s->next[0]) strlcpy(s->next, name, sizeof(s->next));
  if (s->current[0] && strcmp(name, s->current) == 0) s->seenCurrent = true;
  if (s->wanted[0] && strcmp(name, s->wanted) == 0) s->wantedFound = true;
}

void showNotice(Notice kind, const char *detail) {
  if (shownNotice == kind) return;
  tft.fillScreen(TFT_BLACK);
  if (kind == NOTICE_EMPTY) {
    display::drawMessage("No pictures", "Upload JPG or GIF in the web UI");
  } else {
    display::drawMessage("Picture error", detail, TFT_YELLOW);
  }
  shownNotice = kind;
}

// Draws or starts `name`. Returns whether it is on screen (then `phase` says how).
bool show(const char *name) {
  char path[48];
  snprintf(path, sizeof(path), "%s/%s", config::DIR_IMAGE, name);
  strlcpy(current, name, sizeof(current));
  onScreen = false;

  // The GIF decoder and a JPG decode never need to coexist, and a new GIF needs the old
  // one's memory: always start from nothing open.
  media::gifClose();

  bool gif = media::isGif(name);
  bool ok;
  if (gif) {
    ok = media::gifOpenCentered(path, 0, 0, config::SCREEN_W, config::SCREEN_H, true);
    if (ok) tft.fillScreen(TFT_BLACK);   // only now: a failed open must not wipe a notice
  } else {
    ok = media::drawJpgFit(path, 0, 0, config::SCREEN_W, config::SCREEN_H);
  }
  if (ok) {
    phase = gif ? PH_GIF : PH_JPG;
    onScreen = true;
    slotStart = millis();
    shownNotice = NOTICE_NONE;
  }
  return ok;
}

void pick() {
  const settings::Settings &s = settings::get();
  Scan sc;
  memset(&sc, 0, sizeof(sc));
  sc.current = current;
  sc.wanted = s.albumAuto ? "" : s.albumFile;
  media::listDir(config::DIR_IMAGE, scanFile, &sc);
  lastCount = sc.count;

  if (sc.count == 0) {
    media::gifClose();
    current[0] = '\0';
    advance = false;
    onScreen = false;
    failures = 0;
    showNotice(NOTICE_EMPTY, "");
    phase = PH_NOTICE;
    nextTry = millis() + RESCAN_MS;
    return;
  }

  const char *target;
  if (!s.albumAuto) {
    target = sc.wantedFound ? s.albumFile : sc.first;
  } else if (!advance && current[0] && sc.seenCurrent) {
    target = current;   // show the same picture again (repaint, or the GIF was closed under us)
  } else {
    target = sc.next[0] ? sc.next : sc.first;
  }

  // The slot ended and the next picture is the one already showing (a folder with a single
  // picture): keep it, do not decode it or restart the GIF for nothing.
  if (advance && onScreen && strcmp(target, current) == 0) {
    advance = false;
    slotStart = millis();
    phase = media::gifIsOpen() ? PH_GIF : PH_JPG;
    return;
  }
  advance = false;

  char name[config::MAX_FILE_NAME + 1];
  strlcpy(name, target, sizeof(name));   // `target` may point at `current`, which show() rewrites
  if (show(name)) {
    if (phase == PH_JPG) failures = 0;   // a GIF proves itself with its first decoded frame
    return;
  }

  // Could not show it: move on to the next one on the following pass, unless every picture
  // has failed in a row; then say why and wait before trying them all again.
  advance = true;
  if (++failures >= sc.count) {
    failures = 0;
    showNotice(NOTICE_ERROR, media::lastError());
    phase = PH_NOTICE;
    nextTry = millis() + RETRY_MS;
  } else {
    phase = PH_PICK;
  }
}

}  // namespace

void screenAlbumEnter() {
  phase = PH_PICK;
  current[0] = '\0';
  advance = false;
  onScreen = false;
  failures = 0;
  lastCount = 0;
  shownNotice = NOTICE_NONE;
  slotStart = nextTry = millis();
}

void screenAlbumUpdate(bool full) {
  const settings::Settings &s = settings::get();
  uint32_t now = millis();

  if (full) {
    // The manager cleared the screen (first draw, or a setting changed): show the picture
    // again, from the start, without moving on.
    media::gifClose();
    shownNotice = NOTICE_NONE;
    advance = false;
    onScreen = false;
    failures = 0;
    phase = PH_PICK;
  }

  const uint32_t slotMs = (uint32_t)s.albumInterval * 1000UL;

  switch (phase) {
    case PH_PICK:
      pick();
      break;

    case PH_JPG:
      if (s.albumAuto && now - slotStart >= slotMs) {
        advance = true;
        phase = PH_PICK;
      }
      break;

    case PH_GIF:
      if (!media::gifIsOpen()) {
        // Closed behind our back (a weather fetch needs the memory): start it again.
        advance = false;
        onScreen = false;
        phase = PH_PICK;
      } else if (s.albumAuto && now - slotStart >= slotMs) {
        advance = true;   // pick() closes it, unless it is also the next picture
        phase = PH_PICK;
      } else if (media::gifPlayFrame()) {
        failures = 0;     // it decodes: whatever failed before is behind us
      } else {
        // It stopped decoding: treat it like a picture that cannot be shown.
        Serial.printf_P(PSTR("[album] %s: %s\n"), current, media::lastError());
        media::gifClose();
        onScreen = false;
        advance = true;
        if (++failures >= (lastCount ? lastCount : 1)) {   // every picture failed in a row
          failures = 0;
          showNotice(NOTICE_ERROR, media::lastError());
          phase = PH_NOTICE;
          nextTry = now + RETRY_MS;
        } else {
          phase = PH_PICK;
        }
      }
      break;

    case PH_NOTICE:
      if ((int32_t)(now - nextTry) >= 0) phase = PH_PICK;
      break;
  }
}

void screenAlbumLeave() { media::gifClose(); }
