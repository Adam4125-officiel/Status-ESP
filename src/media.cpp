// STUB (phase 1): placeholder so the firmware builds. The real JPG/GIF code replaces this
// file. listDir() is already real because the screen manager uses it.
#include "media.h"

#include <LittleFS.h>

// Pulled in here only so that phase 1 proves the pinned libraries resolve and compile;
// nothing below uses them, so the linker drops their code.
#include <AnimatedGIF.h>
#include <TJpg_Decoder.h>

namespace media {

static const char *errorText = "media module not installed";

bool drawJpg(const char *, int16_t, int16_t) { return false; }
bool gifOpen(const char *, int16_t, int16_t, bool) { return false; }
bool gifPlayFrame() { return false; }
void gifClose() {}
bool gifIsOpen() { return false; }
const char *lastError() { return errorText; }

void listDir(const char *dir, FileCallback cb, void *ctx) {
  if (!cb) return;
  Dir d = LittleFS.openDir(dir);
  while (d.next()) {
    if (d.isFile()) cb(d.fileName().c_str(), d.fileSize(), ctx);
  }
}

}  // namespace media
