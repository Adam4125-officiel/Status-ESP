// STUB (phase 1): theme album (JPG / GIF slideshow from /image). The real screen
// replaces this file; the three functions are the screen contract documented in display.h.
#include "display.h"

void screenAlbumEnter() {}
void screenAlbumUpdate(bool full) {
  if (full) display::drawMessage("Album", "coming in this build");
}
void screenAlbumLeave() {}
