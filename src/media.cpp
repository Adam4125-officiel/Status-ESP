// Pictures: JPG through the tjpgd decoder that ships inside the TJpg_Decoder library, and
// animated GIF through AnimatedGIF, both read from LittleFS and pushed straight to the screen
// (no sprite, no full-screen buffer).
//
// Why tjpgd directly instead of the library's TJpg_Decoder class: that class pulls in the SD
// library, SdFat and the whole SPIFFS implementation (its User_Config.h defines
// TJPGD_LOAD_SD_LIBRARY unconditionally and its file functions default to SPIFFS), about
// 70 KB of flash this firmware does not have, and it keeps a 3.5 KB work area in RAM for ever.
// jd_prepare() / jd_decomp() are plain C functions in their own object file, so calling them
// links none of that, and the work area is allocated only while a picture is being decoded.
//
// Colours: both decoders produce native-endian RGB565 and TFT_eSPI wants the bytes swapped
// on the wire, hence tft.setSwapBytes(true) before every push (nothing else in the firmware
// uses pushImage, so it can stay on).
//
// The GIF decoder is the memory problem: AnimatedGIF is ~24.5 KB in ONE block while the idle
// heap is ~35-40 KB. It is therefore allocated with `new` only while a GIF is open, its size
// is checked against ESP.getMaxFreeBlockSize() first (a failure is an error message, never a
// crash), and gifClose() frees all of it. The weather fetch closes the GIF before it starts.
#include "media.h"

#include <AnimatedGIF.h>
#include <LittleFS.h>
#include <tjpgd.h>

#include <new>

#include "config.h"
#include "display.h"

namespace media {

namespace {

const char *errorText = "";

// Heap that must still be free once the GIF decoder is allocated, so that the web server
// (requests, uploads, the settings JSON) keeps working while a GIF plays.
const uint32_t HEAP_LEFT_AFTER_GIF = 6144;
// Browsers treat a frame delay of 10 ms or less as 100 ms; so does this.
const uint32_t MIN_FRAME_DELAY_MS = 10;
const uint32_t DEFAULT_FRAME_DELAY_MS = 100;

void clearError() { errorText = ""; }

bool fail(const char *what, const char *path = nullptr) {
  errorText = what;
  Serial.printf_P(PSTR("[media] %s%s%s\n"), what, path ? ": " : "", path ? path : "");
  return false;
}

// --- JPG -------------------------------------------------------------------------------

struct JpgJob {
  fs::File *file;
  int16_t x, y;   // screen position of the picture's top-left corner
};

// tjpgd reads the stream through this: buf == nullptr means "skip len bytes".
size_t jpgInput(JDEC *jd, uint8_t *buf, size_t len) {
  fs::File *f = static_cast<JpgJob *>(jd->device)->file;
  size_t left = f->available();
  if (left < len) len = left;
  if (buf) return f->read(buf, len);
  f->seek(f->position() + len, fs::SeekSet);
  return len;
}

// tjpgd hands over every decoded block (an MCU, up to 16x16) here. pushImage clips to the
// screen, so a picture larger than the screen simply loses its right and bottom edges.
int jpgOutput(JDEC *jd, void *bitmap, JRECT *r) {
  JpgJob *job = static_cast<JpgJob *>(jd->device);
  int16_t y = job->y + r->top;
  if (y >= config::SCREEN_H) return 0;   // everything below is off screen: stop decoding
  tft.pushImage(job->x + r->left, y, r->right + 1 - r->left, r->bottom + 1 - r->top, static_cast<uint16_t *>(bitmap));
  return 1;
}

const char *jpgErrorText(JRESULT r) {
  switch (r) {
    case JDR_INP: return "cannot read the file";
    case JDR_MEM1:
    case JDR_MEM2: return "not enough memory";
    case JDR_FMT1: return "not a valid JPEG";
    case JDR_FMT2: return "JPEG type not supported";
    case JDR_FMT3: return "progressive JPEG not supported";
    default: return "cannot decode the JPEG";
  }
}

// Paints black over the part of the box a picture of iw x ih at (x, y) leaves uncovered.
// Never touches the picture's own rectangle, so a full-size picture costs nothing and does
// not flash black before it appears.
void clearAround(int16_t bx, int16_t by, int16_t bw, int16_t bh, int16_t x, int16_t y, int16_t iw, int16_t ih) {
  int16_t x2 = x + iw, y2 = y + ih;
  if (y > by) tft.fillRect(bx, by, bw, y - by, TFT_BLACK);
  if (y2 < by + bh) tft.fillRect(bx, y2, bw, by + bh - y2, TFT_BLACK);
  if (x > bx) tft.fillRect(bx, y, x - bx, ih, TFT_BLACK);
  if (x2 < bx + bw) tft.fillRect(x2, y, bx + bw - x2, ih, TFT_BLACK);
}

// Decodes `path`. fit == false: top-left corner at (x, y), full size. fit == true: reduced by
// 1/2, 1/4 or 1/8 as needed to fit the box and centred in it (x, y are ignored).
bool decodeJpg(const char *path, bool fit, int16_t x, int16_t y, int16_t bx, int16_t by, int16_t bw, int16_t bh) {
  fs::File f = LittleFS.open(path, "r");
  if (!f || f.isDirectory()) return fail("file not found", path);
  if (f.size() == 0) {
    f.close();
    return fail("empty file", path);
  }

  uint8_t *pool = new (std::nothrow) uint8_t[TJPGD_WORKSPACE_SIZE];   // freed before returning
  if (!pool) {
    f.close();
    return fail("not enough memory", path);
  }

  JpgJob job = {&f, x, y};
  JDEC jd;
  memset(&jd, 0, sizeof(jd));   // jd_prepare() keeps the .swap flag it finds: must be 0
  JRESULT r = jd_prepare(&jd, jpgInput, pool, TJPGD_WORKSPACE_SIZE, &job);
  const char *problem = nullptr;
  if (r != JDR_OK) {
    problem = jpgErrorText(r);
  } else {
    uint8_t shift = 0;   // log2 of the reduction, which is what jd_decomp() takes
    if (fit) {
      while (shift < 3 && (((jd.width + (1 << shift) - 1) >> shift) > bw || ((jd.height + (1 << shift) - 1) >> shift) > bh)) shift++;
      int16_t dw = (int16_t)((jd.width + (1 << shift) - 1) >> shift);
      int16_t dh = (int16_t)((jd.height + (1 << shift) - 1) >> shift);
      if (dw > bw || dh > bh) {
        problem = "picture too large";
      } else {
        job.x = bx + (bw - dw) / 2;
        job.y = by + (bh - dh) / 2;
        clearAround(bx, by, bw, bh, job.x, job.y, dw, dh);
      }
    }
    if (!problem) {
      tft.setSwapBytes(true);
      r = jd_decomp(&jd, jpgOutput, shift);
      if (r != JDR_OK && r != JDR_INTR) problem = jpgErrorText(r);   // INTR: we stopped it ourselves
    }
  }
  delete[] pool;
  f.close();
  return problem ? fail(problem, path) : true;
}

// --- GIF -------------------------------------------------------------------------------

// Everything a playing GIF needs, in one allocation that gifClose() frees in one go.
struct Player {
  AnimatedGIF gif;
  fs::File file;
  uint16_t line[config::SCREEN_W];   // one row of RGB565 for the draw callback
  int16_t ox, oy;                    // screen position of the canvas's top-left corner
  bool loop;
  bool finished;                     // loop == false and the last frame has been drawn
  uint8_t emptyStreak;               // consecutive "no frame" answers (a GIF without frames)
  uint32_t nextAt;                   // millis() at which the next frame is due
};

Player *player = nullptr;
Player *opening = nullptr;           // set only while AnimatedGIF::open() runs its callbacks

void *gifOpenFile(const char *, int32_t *size) {
  if (!opening || !opening->file) return nullptr;
  *size = (int32_t)opening->file.size();
  return &opening->file;
}

void gifCloseFile(void *handle) {
  if (handle) static_cast<fs::File *>(handle)->close();
}

int32_t gifReadFile(GIFFILE *g, uint8_t *buf, int32_t len) {
  fs::File *f = static_cast<fs::File *>(g->fHandle);
  int32_t left = g->iSize - g->iPos;
  if (left < len) len = left;
  if (len <= 0) return 0;
  int32_t n = (int32_t)f->read(buf, (size_t)len);
  if (n < 0) n = 0;
  g->iPos += n;
  return n;
}

int32_t gifSeekFile(GIFFILE *g, int32_t pos) {
  fs::File *f = static_cast<fs::File *>(g->fHandle);
  if (pos < 0) pos = 0;
  else if (pos >= g->iSize) pos = g->iSize - 1;
  f->seek((uint32_t)pos, fs::SeekSet);
  g->iPos = (int32_t)f->position();
  return g->iPos;
}

// Draws one decoded row. Rows come as 8-bit palette indexes; transparent pixels are skipped
// (the previous frame stays under them), except after a "restore to background" frame, where
// they are painted with the background colour instead. Clipped to the screen.
void gifDraw(GIFDRAW *d) {
  Player *p = static_cast<Player *>(d->pUser);
  if (!p) return;
  int y = p->oy + d->iY + d->y;
  if (y < 0 || y >= config::SCREEN_H) return;

  int count = d->iWidth;
  int x0 = p->ox + d->iX;
  int skip = 0;
  if (x0 < 0) {
    skip = -x0;
    x0 = 0;
  }
  if (x0 + (count - skip) > config::SCREEN_W) count = config::SCREEN_W - x0 + skip;
  if (count - skip <= 0) return;

  const uint16_t *palette = d->pPalette;
  uint8_t *s = d->pPixels;
  bool transparent = d->ucHasTransparency;
  if (transparent && d->ucDisposalMethod == 2) {
    for (int i = 0; i < d->iWidth; i++) {
      if (s[i] == d->ucTransparent) s[i] = d->ucBackground;
    }
    transparent = false;
  }

  if (!transparent) {
    int n = count - skip;
    for (int i = 0; i < n; i++) p->line[i] = palette[s[skip + i]];
    tft.pushImage(x0, y, n, 1, p->line);
    return;
  }

  // Runs of opaque pixels are pushed; runs of transparent ones are stepped over.
  int i = skip;
  while (i < count) {
    while (i < count && s[i] == d->ucTransparent) i++;
    int start = i, n = 0;
    while (i < count && s[i] != d->ucTransparent) p->line[n++] = palette[s[i++]];
    if (n) tft.pushImage(x0 + (start - skip), y, n, 1, p->line);
  }
}

const char *gifErrorText(int code) {
  switch (code) {
    case GIF_TOO_WIDE: return "GIF too large";
    case GIF_UNSUPPORTED_FEATURE: return "GIF type not supported";
    case GIF_FILE_NOT_OPEN: return "file not found";
    case GIF_EARLY_EOF: return "GIF truncated";
    case GIF_EMPTY_FRAME: return "GIF has no frames";
    case GIF_BAD_FILE: return "not a valid GIF";
    case GIF_ERROR_MEMORY: return "not enough memory";
    default: return "cannot decode the GIF";
  }
}

// bx.. is the box to centre the canvas in; with bw == 0 the canvas goes at exactly (x, y).
bool openGif(const char *path, int16_t x, int16_t y, bool loop, int16_t bx, int16_t by, int16_t bw, int16_t bh) {
  gifClose();
  clearError();

  // The decoder is one ~24.5 KB block. Refuse it, with a reason, instead of crashing the
  // heap, and also when it would leave the web server too little to work with.
  const uint32_t need = sizeof(Player);
  if (ESP.getMaxFreeBlockSize() < need + 1024 || ESP.getFreeHeap() < need + HEAP_LEFT_AFTER_GIF) {
    return fail("not enough memory", path);
  }

  Player *p = new (std::nothrow) Player;
  if (!p) return fail("not enough memory", path);

  p->file = LittleFS.open(path, "r");
  if (!p->file || p->file.isDirectory()) {
    p->file.close();
    delete p;
    return fail("file not found", path);
  }
  if (p->file.size() == 0) {
    p->file.close();
    delete p;
    return fail("empty file", path);
  }

  p->gif.begin(GIF_PALETTE_RGB565_LE);
  opening = p;
  int ok = p->gif.open(path, gifOpenFile, gifCloseFile, gifReadFile, gifSeekFile, gifDraw);
  opening = nullptr;
  if (!ok) {
    int code = p->gif.getLastError();
    p->file.close();
    delete p;
    return fail(gifErrorText(code), path);
  }

  if (bw > 0) {
    x = bx + (bw - p->gif.getCanvasWidth()) / 2;
    y = by + (bh - p->gif.getCanvasHeight()) / 2;
  }
  p->ox = x;
  p->oy = y;
  p->loop = loop;
  p->finished = false;
  p->emptyStreak = 0;
  p->nextAt = millis();
  player = p;
  tft.setSwapBytes(true);
  return true;
}

}  // namespace

// --- Public: JPG -----------------------------------------------------------------------

bool drawJpg(const char *path, int16_t x, int16_t y) {
  clearError();
  return decodeJpg(path, false, x, y, 0, 0, 0, 0);
}

bool drawJpgFit(const char *path, int16_t boxX, int16_t boxY, int16_t boxW, int16_t boxH) {
  clearError();
  return decodeJpg(path, true, 0, 0, boxX, boxY, boxW, boxH);
}

// --- Public: GIF -----------------------------------------------------------------------

bool gifOpen(const char *path, int16_t x, int16_t y, bool loop) {
  return openGif(path, x, y, loop, 0, 0, 0, 0);
}

bool gifOpenCentered(const char *path, int16_t boxX, int16_t boxY, int16_t boxW, int16_t boxH, bool loop) {
  return openGif(path, 0, 0, loop, boxX, boxY, boxW, boxH);
}

bool gifPlayFrame() {
  Player *p = player;
  if (!p) return false;
  uint32_t now = millis();
  if ((int32_t)(now - p->nextAt) < 0) return true;   // the current frame's delay is not over
  if (p->finished) return false;                     // loop == false: last frame shown, its delay elapsed

  clearError();
  tft.setSwapBytes(true);
  int delayMs = 0;
  int rc = p->gif.playFrame(false, &delayMs, p);
  if (rc < 0) {
    fail(gifErrorText(p->gif.getLastError()));
    return false;
  }
  if (rc == 0 && p->gif.getLastError() == GIF_EMPTY_FRAME) {
    // The file ran out without another frame (trailing data). Wrap round, but give up on a
    // file that has no frame at all instead of spinning on it.
    p->gif.reset();   // clears the error and seeks to the start
    if (!p->loop) {
      p->finished = true;
      p->nextAt = now;
      return false;
    }
    if (++p->emptyStreak >= 2) {
      fail("GIF has no frames");
      return false;
    }
    return true;
  }
  p->emptyStreak = 0;
  if (delayMs <= (int)MIN_FRAME_DELAY_MS) delayMs = DEFAULT_FRAME_DELAY_MS;
  p->nextAt = now + (uint32_t)delayMs;
  if (rc == 0 && !p->loop) p->finished = true;   // that was the last frame
  return true;
}

void gifClose() {
  Player *p = player;
  if (!p) return;
  player = nullptr;
  p->gif.close();   // closes the file through gifCloseFile()
  delete p;
}

bool gifIsOpen() { return player != nullptr; }

const char *lastError() { return errorText; }

// --- Public: files ---------------------------------------------------------------------

void listDir(const char *dir, FileCallback cb, void *ctx) {
  if (!cb) return;
  Dir d = LittleFS.openDir(dir);
  while (d.next()) {
    if (d.isFile()) cb(d.fileName().c_str(), d.fileSize(), ctx);
  }
}

}  // namespace media
