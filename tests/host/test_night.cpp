// Host test of the night window (night.h): fixed times across midnight, sunset -> sunrise, the fallback to the fixed
// times, and the two clocks (the screen's and the weather city's). Pure: nothing to link.
#include <stdio.h>

#include <initializer_list>

#include "night.h"

static int failures = 0, checks = 0;
#define CHECK(cond)                                                    \
  do {                                                                 \
    checks++;                                                          \
    if (!(cond)) {                                                     \
      failures++;                                                      \
      printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);           \
    }                                                                  \
  } while (0)

static int hm(int h, int m) { return h * 60 + m; }

int main() {
  // contains(): inside a window that stays in one day, and one that crosses midnight; the end is excluded.
  CHECK(night::contains(hm(12, 0), hm(9, 0), hm(17, 0)));
  CHECK(night::contains(hm(9, 0), hm(9, 0), hm(17, 0)));
  CHECK(!night::contains(hm(17, 0), hm(9, 0), hm(17, 0)));
  CHECK(!night::contains(hm(8, 59), hm(9, 0), hm(17, 0)));
  CHECK(night::contains(hm(23, 30), hm(22, 0), hm(7, 0)));
  CHECK(night::contains(hm(0, 0), hm(22, 0), hm(7, 0)));
  CHECK(night::contains(hm(6, 59), hm(22, 0), hm(7, 0)));
  CHECK(!night::contains(hm(7, 0), hm(22, 0), hm(7, 0)));
  CHECK(!night::contains(hm(15, 0), hm(22, 0), hm(7, 0)));
  CHECK(night::contains(hm(22, 0), hm(22, 0), hm(7, 0)));
  CHECK(!night::contains(hm(12, 0), hm(8, 0), hm(8, 0)));   // equal bounds: no window
  CHECK(!night::contains(hm(8, 0), hm(8, 0), hm(8, 0)));
  CHECK(night::contains(1439, hm(23, 59), hm(0, 1)) && night::contains(0, hm(23, 59), hm(0, 1)));

  // usesSun(): the switch, the weather, and real minutes.
  CHECK(night::usesSun(true, true, hm(7, 42), hm(17, 5)));
  CHECK(!night::usesSun(false, true, hm(7, 42), hm(17, 5)));
  CHECK(!night::usesSun(true, false, hm(7, 42), hm(17, 5)));    // no weather yet
  CHECK(!night::usesSun(true, true, -1, hm(17, 5)));            // polar night / day: one time missing
  CHECK(!night::usesSun(true, true, hm(7, 42), -1));
  CHECK(!night::usesSun(true, true, -1, -1));
  CHECK(!night::usesSun(true, true, 1440, hm(17, 5)));          // not a minute of a day
  CHECK(!night::usesSun(true, true, hm(12, 0), hm(12, 0)));     // equal: no night to speak of

  // inWindow(): fixed times when the sun is off or unknown. The screen's minute is the one that counts.
  CHECK(night::inWindow(false, true, hm(7, 42), hm(17, 5), hm(22, 0), hm(7, 0), hm(23, 0), hm(12, 0)));
  CHECK(!night::inWindow(false, true, hm(7, 42), hm(17, 5), hm(22, 0), hm(7, 0), hm(12, 0), hm(23, 0)));
  CHECK(night::inWindow(true, false, hm(7, 42), hm(17, 5), hm(22, 0), hm(7, 0), hm(23, 0), hm(12, 0)));   // fallback
  CHECK(!night::inWindow(true, false, hm(7, 42), hm(17, 5), hm(22, 0), hm(7, 0), hm(18, 0), hm(18, 0)));  // fallback: 18h is day
  CHECK(night::inWindow(true, true, -1, hm(17, 5), hm(22, 0), hm(7, 0), hm(23, 0), hm(12, 0)));           // unknown: fixed
  CHECK(!night::inWindow(true, true, hm(7, 42), -1, hm(22, 0), hm(7, 0), hm(12, 0), hm(23, 0)));

  // Sunset 17:05 -> sunrise 07:42 (a winter day): night from 17:05 and until 07:41 included.
  for (int m : {hm(17, 5), hm(18, 0), hm(23, 59), hm(0, 0), hm(3, 0), hm(7, 41)})
    CHECK(night::inWindow(true, true, hm(7, 42), hm(17, 5), hm(22, 0), hm(7, 0), 0, m));
  for (int m : {hm(7, 42), hm(12, 0), hm(17, 4)})
    CHECK(!night::inWindow(true, true, hm(7, 42), hm(17, 5), hm(22, 0), hm(7, 0), 0, m));

  // Two clocks: the owner's screen shows 09:00 (a manual offset) while it is 15:00 in the city; sunset there is
  // 17:05, so it is day, whatever the fixed window says about 09:00 (night 08:00 - 10:00 on the screen's clock).
  CHECK(!night::inWindow(true, true, hm(7, 42), hm(17, 5), hm(8, 0), hm(10, 0), hm(9, 0), hm(15, 0)));
  CHECK(night::inWindow(false, true, hm(7, 42), hm(17, 5), hm(8, 0), hm(10, 0), hm(9, 0), hm(15, 0)));
  // And the other way: 12:00 on the screen, 18:00 in the city (after sunset): night with the sun, day without.
  CHECK(night::inWindow(true, true, hm(7, 42), hm(17, 5), hm(22, 0), hm(7, 0), hm(12, 0), hm(18, 0)));
  CHECK(!night::inWindow(false, true, hm(7, 42), hm(17, 5), hm(22, 0), hm(7, 0), hm(12, 0), hm(18, 0)));

  // A summer day in the far north: sunrise 03:10, sunset 22:50 (a short night).
  CHECK(night::inWindow(true, true, hm(3, 10), hm(22, 50), hm(22, 0), hm(7, 0), 0, hm(1, 0)));
  CHECK(!night::inWindow(true, true, hm(3, 10), hm(22, 50), hm(22, 0), hm(7, 0), 0, hm(22, 49)));
  CHECK(!night::inWindow(true, true, hm(3, 10), hm(22, 50), hm(22, 0), hm(7, 0), 0, hm(3, 10)));
  printf("night: %d checks, %d failed\n", checks, failures);
  return failures ? 1 : 0;
}
