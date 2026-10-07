// Weather icons: WMO codes drawn with TFT_eSPI primitives (circles, rounded rectangles,
// triangles), scaled to any size. Nothing here is copied from the stock firmware.
//
// Every shape is computed from `size` and the centre, so the same code serves the 80 px
// weather screen and the ~48 px forecast. Drawing is opaque and assumes a black background
// (a cloud in front of the sun gets a thin black outline to separate the two, the moon is
// carved with a black disc). Nothing is allocated.
#include "icons.h"

#include <math.h>

#include "display.h"

namespace icons {

namespace {

constexpr uint16_t rgb(uint8_t r, uint8_t g, uint8_t b) {
  return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

const uint16_t SUN_COLOR = rgb(255, 200, 0);
const uint16_t MOON_COLOR = rgb(235, 235, 200);
const uint16_t CLOUD_LIGHT = rgb(205, 210, 220);
const uint16_t CLOUD_MID = rgb(150, 156, 172);
const uint16_t CLOUD_DARK = rgb(105, 110, 130);
const uint16_t RAIN_COLOR = rgb(80, 160, 255);
const uint16_t SNOW_COLOR = rgb(235, 245, 255);
const uint16_t BOLT_COLOR = rgb(255, 220, 40);
const uint16_t FOG_COLOR = rgb(160, 166, 178);

enum Kind : uint8_t { K_CLEAR, K_PARTLY, K_CLOUDY, K_FOG, K_DRIZZLE, K_RAIN, K_SNOW, K_SHOWERS, K_THUNDER };

Kind kindOf(uint8_t code) {
  switch (code) {
    case 0:
    case 1: return K_CLEAR;
    case 2: return K_PARTLY;
    case 3: return K_CLOUDY;
    case 45:
    case 48: return K_FOG;
    case 51:
    case 53:
    case 55:
    case 56:
    case 57: return K_DRIZZLE;
    case 61:
    case 63:
    case 65:
    case 66:
    case 67: return K_RAIN;
    case 71:
    case 73:
    case 75:
    case 77:
    case 85:
    case 86: return K_SNOW;
    case 80:
    case 81:
    case 82: return K_SHOWERS;
    case 95:
    case 96:
    case 99: return K_THUNDER;
    default: return K_CLOUDY;
  }
}

// `base` scaled by a fraction.
inline int16_t sc(int16_t base, float fraction) { return (int16_t)lroundf(base * fraction); }

// A line `width` pixels thick (two triangles), for rays, drops and the lightning bolt.
void thickLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1, int16_t width, uint16_t color) {
  if (width <= 1) {
    tft.drawLine(x0, y0, x1, y1, color);
    return;
  }
  float dx = x1 - x0, dy = y1 - y0;
  float len = sqrtf(dx * dx + dy * dy);
  if (len < 0.5f) {
    tft.fillCircle(x0, y0, width / 2, color);
    return;
  }
  float nx = -dy / len * width * 0.5f, ny = dx / len * width * 0.5f;
  int16_t ax = (int16_t)lroundf(x0 + nx), ay = (int16_t)lroundf(y0 + ny);
  int16_t bx = (int16_t)lroundf(x0 - nx), by = (int16_t)lroundf(y0 - ny);
  int16_t cx = (int16_t)lroundf(x1 + nx), cy = (int16_t)lroundf(y1 + ny);
  int16_t dx2 = (int16_t)lroundf(x1 - nx), dy2 = (int16_t)lroundf(y1 - ny);
  tft.fillTriangle(ax, ay, bx, by, cx, cy, color);
  tft.fillTriangle(bx, by, dx2, dy2, cx, cy, color);
}

// Stroke thickness for lines: 1 px up to ~35 px icons, 2 at 48, 3 at 80.
inline int16_t stroke(int16_t size) { return size < 36 ? 1 : (int16_t)(size / 24); }

// Cloud of overall width w (height about 0.65 w) centred on (cx, cy). `grow` inflates every
// part by that many pixels: a black cloud drawn first with grow > 0 is the outline that
// separates a cloud from whatever is behind it.
void cloud(int16_t cx, int16_t cy, int16_t w, uint16_t color, int16_t grow = 0) {
  int16_t top = cy - sc(w, 0.325f);
  int16_t bottom = cy + sc(w, 0.325f);
  int16_t baseH = sc(w, 0.36f);
  int16_t rx = cx - w / 2 - grow, ry = bottom - baseH - grow;
  int16_t rw = w + 2 * grow, rh = baseH + 2 * grow;
  tft.fillRoundRect(rx, ry, rw, rh, rh / 2, color);
  tft.fillCircle(cx + sc(w, 0.07f), top + sc(w, 0.27f), sc(w, 0.27f) + grow, color);
  tft.fillCircle(cx - sc(w, 0.20f), bottom - baseH + sc(w, 0.05f), sc(w, 0.19f) + grow, color);
}

// Sun: a disc of radius r with eight rays.
void sun(int16_t cx, int16_t cy, int16_t r, int16_t thick) {
  static const int16_t DIR[8][2] = {{1000, 0},  {707, 707},   {0, 1000},  {-707, 707},
                                    {-1000, 0}, {-707, -707}, {0, -1000}, {707, -707}};
  tft.fillCircle(cx, cy, r, SUN_COLOR);
  int16_t from = r + r * 35 / 100, to = r + r * 80 / 100;
  for (uint8_t i = 0; i < 8; i++) {
    int16_t x0 = cx + (int16_t)((int32_t)DIR[i][0] * from / 1000);
    int16_t y0 = cy + (int16_t)((int32_t)DIR[i][1] * from / 1000);
    int16_t x1 = cx + (int16_t)((int32_t)DIR[i][0] * to / 1000);
    int16_t y1 = cy + (int16_t)((int32_t)DIR[i][1] * to / 1000);
    thickLine(x0, y0, x1, y1, thick, SUN_COLOR);
  }
}

// Crescent moon of radius r: a disc with a black disc carved out of its upper right.
void moon(int16_t cx, int16_t cy, int16_t r) {
  tft.fillCircle(cx, cy, r, MOON_COLOR);
  tft.fillCircle(cx + r * 45 / 100, cy - r * 30 / 100, r * 85 / 100, TFT_BLACK);
}

// Sun by day, moon by night, for the "clear" and "partly cloudy" icons.
void body(int16_t cx, int16_t cy, int16_t r, int16_t thick, bool isDay) {
  if (isDay) {
    sun(cx, cy, r, thick);
  } else {
    moon(cx, cy, r + r / 4);
  }
}

// `count` rain drops (slanted lines) in a row, left to right, starting at y.
void rain(int16_t cx, int16_t y, int16_t size, uint8_t count, int16_t len, uint16_t color) {
  int16_t t = stroke(size);
  int16_t spacing = sc(size, 0.19f);
  int16_t x = cx - (count - 1) * spacing / 2;
  for (uint8_t i = 0; i < count; i++, x += spacing) {
    int16_t yy = y + ((i & 1) ? sc(size, 0.05f) : 0);
    thickLine(x + len / 3, yy, x - len / 3, yy + len, t, color);
  }
}

// A small asterisk.
void flake(int16_t x, int16_t y, int16_t r, uint16_t color) {
  tft.drawLine(x - r, y, x + r, y, color);
  tft.drawLine(x, y - r, x, y + r, color);
  int16_t d = r * 7 / 10;
  tft.drawLine(x - d, y - d, x + d, y + d, color);
  tft.drawLine(x - d, y + d, x + d, y - d, color);
}

}  // namespace

void drawWeather(int16_t cx, int16_t cy, int16_t size, uint8_t wmoCode, bool isDay) {
  int16_t t = stroke(size);
  int16_t outline = size >= 40 ? 2 : 1;

  switch (kindOf(wmoCode)) {
    case K_CLEAR:
      body(cx, cy, sc(size, 0.24f), t, isDay);
      if (!isDay && size >= 40) {  // two stars
        tft.fillCircle(cx + sc(size, 0.30f), cy - sc(size, 0.30f), 1, MOON_COLOR);
        tft.fillCircle(cx - sc(size, 0.30f), cy + sc(size, 0.26f), 1, MOON_COLOR);
      }
      break;

    case K_PARTLY:
      body(cx - sc(size, 0.12f), cy - sc(size, 0.14f), sc(size, 0.17f), t, isDay);
      cloud(cx + sc(size, 0.06f), cy + sc(size, 0.12f), sc(size, 0.66f), TFT_BLACK, outline);
      cloud(cx + sc(size, 0.06f), cy + sc(size, 0.12f), sc(size, 0.66f), CLOUD_LIGHT);
      break;

    case K_CLOUDY:
      cloud(cx - sc(size, 0.16f), cy - sc(size, 0.16f), sc(size, 0.50f), CLOUD_MID);
      cloud(cx + sc(size, 0.04f), cy + sc(size, 0.06f), sc(size, 0.78f), TFT_BLACK, outline);
      cloud(cx + sc(size, 0.04f), cy + sc(size, 0.06f), sc(size, 0.78f), CLOUD_LIGHT);
      break;

    case K_FOG: {
      cloud(cx, cy - sc(size, 0.16f), sc(size, 0.60f), CLOUD_MID);
      int16_t bar = size >= 40 ? size / 20 : 2;
      static const float WIDTH[3] = {0.72f, 0.56f, 0.66f};
      for (uint8_t i = 0; i < 3; i++) {
        int16_t w = sc(size, WIDTH[i]);
        tft.fillRoundRect(cx - w / 2 + (i == 1 ? sc(size, 0.06f) : 0), cy + sc(size, 0.12f) + i * sc(size, 0.13f), w, bar,
                          bar / 2, FOG_COLOR);
      }
      break;
    }

    case K_DRIZZLE: {
      cloud(cx, cy - sc(size, 0.12f), sc(size, 0.74f), CLOUD_LIGHT);
      int16_t r = size >= 56 ? 2 : 1;
      for (uint8_t row = 0; row < 2; row++) {
        for (uint8_t col = 0; col < 3 - row; col++) {
          tft.fillCircle(cx - sc(size, 0.19f) + col * sc(size, 0.19f) + row * sc(size, 0.095f),
                         cy + sc(size, 0.22f) + row * sc(size, 0.14f), r, RAIN_COLOR);
        }
      }
      break;
    }

    case K_RAIN:
      cloud(cx, cy - sc(size, 0.12f), sc(size, 0.74f), CLOUD_MID);
      rain(cx, cy + sc(size, 0.22f), size, 3, sc(size, 0.18f), RAIN_COLOR);
      break;

    case K_SHOWERS:
      body(cx - sc(size, 0.16f), cy - sc(size, 0.22f), sc(size, 0.14f), t, isDay);
      cloud(cx + sc(size, 0.05f), cy - sc(size, 0.06f), sc(size, 0.66f), TFT_BLACK, outline);
      cloud(cx + sc(size, 0.05f), cy - sc(size, 0.06f), sc(size, 0.66f), CLOUD_MID);
      rain(cx, cy + sc(size, 0.22f), size, 3, sc(size, 0.18f), RAIN_COLOR);
      break;

    case K_SNOW: {
      cloud(cx, cy - sc(size, 0.12f), sc(size, 0.74f), CLOUD_LIGHT);
      int16_t r = size >= 56 ? 4 : (size >= 36 ? 3 : 2);
      flake(cx - sc(size, 0.19f), cy + sc(size, 0.27f), r, SNOW_COLOR);
      flake(cx, cy + sc(size, 0.36f), r, SNOW_COLOR);
      flake(cx + sc(size, 0.19f), cy + sc(size, 0.27f), r, SNOW_COLOR);
      break;
    }

    case K_THUNDER: {
      cloud(cx, cy - sc(size, 0.12f), sc(size, 0.74f), CLOUD_DARK);
      int16_t w = size >= 56 ? size / 12 : 2;
      int16_t x0 = cx + sc(size, 0.08f), y0 = cy + sc(size, 0.06f);
      int16_t x1 = cx - sc(size, 0.04f), y1 = cy + sc(size, 0.26f);
      int16_t x2 = cx + sc(size, 0.08f), y2 = cy + sc(size, 0.26f);
      int16_t x3 = cx - sc(size, 0.06f), y3 = cy + sc(size, 0.46f);
      thickLine(x0, y0, x1, y1, w, BOLT_COLOR);
      thickLine(x1, y1, x2, y2, w, BOLT_COLOR);
      thickLine(x2, y2, x3, y3, w, BOLT_COLOR);
      break;
    }
  }
}

const char *describe(uint8_t wmoCode) {
  switch (wmoCode) {
    case 0: return "Clear";
    case 1: return "Mostly clear";
    case 2: return "Partly cloudy";
    case 3: return "Overcast";
    case 45:
    case 48: return "Fog";
    case 51: return "Light drizzle";
    case 53: return "Drizzle";
    case 55: return "Heavy drizzle";
    case 56:
    case 57: return "Freezing drizzle";
    case 61: return "Light rain";
    case 63: return "Rain";
    case 65: return "Heavy rain";
    case 66:
    case 67: return "Freezing rain";
    case 71: return "Light snow";
    case 73: return "Snow";
    case 75: return "Heavy snow";
    case 77: return "Snow grains";
    case 80: return "Light showers";
    case 81: return "Showers";
    case 82: return "Heavy showers";
    case 85: return "Snow showers";
    case 86: return "Heavy snow showers";
    case 95: return "Thunderstorm";
    case 96:
    case 99: return "Thunder and hail";
    default: return "Cloudy";
  }
}

}  // namespace icons
