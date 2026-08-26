// EPD wrapper — init and draw helpers for the toy phone app.
// Wraps epdiy, reusing proven patterns from weather_dashboard / image_test.

#pragma once

#include <epdiy.h>

namespace epd {

constexpr uint8_t kI2cSda         = 39;
constexpr uint8_t kI2cScl         = 40;
constexpr int     kVcomMillivolts = 1560;
constexpr int     kRotation       = EPD_ROT_INVERTED_PORTRAIT;  // 540x960, USB-C bottom

constexpr uint8_t kWhite = 0xFF;
constexpr uint8_t kBlack = 0x00;

constexpr int kWidth  = 540;  // epd_rotated_display_width()
constexpr int kHeight = 960;  // epd_rotated_display_height()

extern EpdiyHighlevelState hl;
extern uint8_t *fb;

void init();

void clear();
void refresh();
void refreshFull();

void fillWhite();
void fillRect(int x, int y, int w, int h, uint8_t color);
void drawHLine(int x, int y, int w, uint8_t color = kBlack);
void drawVLine(int x, int y, int h, uint8_t color = kBlack);
void drawText(const EpdFont *font, int x, int y, const char *s,
              EpdFontFlags flags = (EpdFontFlags)0);
void drawCenterText(const EpdFont *font, int xCenter, int y, const char *s);
void drawRightText(const EpdFont *font, int xRight, int y, const char *s);

}  // namespace epd
