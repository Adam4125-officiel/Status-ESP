// Pictures: JPG decoding (TJpg_Decoder) and animated GIF playback (AnimatedGIF), both
// from LittleFS straight to the screen (the global `tft`, see display.h).
//
// Stock folders are reused read-only except the two the user may upload to / delete from
// (config::DIR_IMAGE "/image", config::DIR_GIF "/gif"); this module only ever READS files.
//
// Threading: no threads, everything is called from loop() through the screens.
//
// Memory is the constraint: ~40 KB of heap is free at idle and AnimatedGIF's decoder
// object alone is ~24.5 KB (sizeof(AnimatedGIF), allocated with new). It must be
// allocated ONLY while a GIF is on screen and freed (gifClose) as soon as it leaves.
// Never use sprites or full-screen buffers. TJpg_Decoder works in MCU blocks and needs
// ~3.5 KB of work area plus the file buffer.
#pragma once

#include <Arduino.h>

namespace media {

// --- JPG -------------------------------------------------------------------------

// Decodes the baseline JPEG at `path` (a LittleFS path) with its top-left corner at
// (x, y), clipped to the screen. BLOCKING for the whole decode (roughly 100-300 ms for
// 240x240): call it only when the picture changes, never on every loop pass. Returns
// false (and sets lastError()) if the file is missing, is not a decodable baseline
// JPEG, or does not fit in memory. Progressive JPEGs are not supported by the decoder.
bool drawJpg(const char *path, int16_t x, int16_t y);

// Like drawJpg(), for a picture of any size: the decoder's 1/2, 1/4 or 1/8 reduction is
// picked so that it fits the box, it is centred in it, and only the part of the box it does
// not cover is painted black (a picture that fills the box causes no black flash). Fails
// with "picture too large" when even 1/8 does not fit. Same blocking behaviour as drawJpg().
bool drawJpgFit(const char *path, int16_t boxX, int16_t boxY, int16_t boxW, int16_t boxH);

// --- GIF -------------------------------------------------------------------------
// At most one GIF is open at a time (gifOpen closes a previous one first).

// Opens the GIF at `path`, to be drawn with its top-left corner at (x, y). Checks
// ESP.getMaxFreeBlockSize() against the decoder's size (plus a safety margin) BEFORE
// allocating: if it does not fit, returns false and sets lastError() instead of
// crashing. loop == true: the animation restarts forever (weather screen);
// loop == false: gifPlayFrame() returns false once the last frame has been shown.
bool gifOpen(const char *path, int16_t x, int16_t y, bool loop = true);

// Like gifOpen(), with the GIF's canvas centred in the given box instead of placed at a
// corner. The box is not cleared: the caller does that once the open succeeded.
bool gifOpenCentered(const char *path, int16_t boxX, int16_t boxY, int16_t boxW, int16_t boxH, bool loop = true);

// Non-blocking, call it on every loop pass while a GIF is open. If the current
// frame's delay has not elapsed it returns true at once without drawing; otherwise it
// decodes and draws exactly one frame and returns true. Returns false when the GIF is
// finished (loop == false), failed to decode, or none is open: the caller then calls
// gifClose() and shows something else.
bool gifPlayFrame();

// Frees the decoder and closes the file. Idempotent, safe to call when nothing is open.
void gifClose();
bool gifIsOpen();

// Short ASCII reason of the last failure ("file not found", "not enough memory"...),
// "" if none. Valid until the next media call.
const char *lastError();

// --- Files -------------------------------------------------------------------------

typedef void (*FileCallback)(const char *name, size_t size, void *ctx);
// Calls cb(name, size, ctx) for every regular file directly inside `dir` (no recursion,
// any extension; filter with isJpg / isGif). `name` is the bare file name. A missing
// folder is not an error: cb is simply never called. Order is the file system's.
void listDir(const char *dir, FileCallback cb, void *ctx);

inline bool hasExt(const char *name, const char *ext) {
  size_t n = strlen(name), e = strlen(ext);
  return n > e && strcasecmp(name + n - e, ext) == 0;
}
inline bool isJpg(const char *name) { return hasExt(name, ".jpg") || hasExt(name, ".jpeg"); }
inline bool isGif(const char *name) { return hasExt(name, ".gif"); }

}  // namespace media
