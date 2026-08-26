#include "epd_wrap.h"

#include <Arduino.h>
#include <cstring>

namespace epd {

EpdiyHighlevelState hl;
uint8_t *fb = nullptr;

void init() {
    epd_init(&epd_board_v7, &ED047TC1, EPD_LUT_64K);
    epd_set_vcom(kVcomMillivolts);
    hl = epd_hl_init(EPD_BUILTIN_WAVEFORM);
    fb = epd_hl_get_framebuffer(&hl);
    epd_set_rotation((EpdRotation)kRotation);

    Serial.printf("[epd] %dx%d ready\n",
                  epd_rotated_display_width(),
                  epd_rotated_display_height());

    epd_poweron();
    epd_fullclear(&hl, 25);
    epd_poweroff();
}

void fillWhite() {
    epd_hl_set_all_white(&hl);
}

void clear() {
    epd_poweron();
    epd_fullclear(&hl, 25);
    epd_poweroff();
}

void refresh() {
    epd_poweron();
    epd_hl_update_screen(&hl, MODE_GC16, 25);
    epd_poweroff();
}

void refreshFull() {
    epd_poweron();
    epd_fullclear(&hl, 25);
    epd_poweroff();
}

void fillRect(int x, int y, int w, int h, uint8_t color) {
    EpdRect r = {.x = x, .y = y, .width = w, .height = h};
    epd_fill_rect(r, color, fb);
}

void drawHLine(int x, int y, int w, uint8_t color) {
    EpdRect r = {.x = x, .y = y, .width = w, .height = 1};
    epd_fill_rect(r, color, fb);
}

void drawVLine(int x, int y, int h, uint8_t color) {
    EpdRect r = {.x = x, .y = y, .width = 1, .height = h};
    epd_fill_rect(r, color, fb);
}

void drawText(const EpdFont *font, int x, int y, const char *s,
              EpdFontFlags flags) {
    int cx = x, cy = y;
    EpdFontProperties props = epd_font_properties_default();
    props.flags = flags;
    epd_write_string(font, s, &cx, &cy, fb, &props);
}

void drawCenterText(const EpdFont *font, int xCenter, int y, const char *s) {
    drawText(font, xCenter, y, s, EPD_DRAW_ALIGN_CENTER);
}

void drawRightText(const EpdFont *font, int xRight, int y, const char *s) {
    drawText(font, xRight, y, s, EPD_DRAW_ALIGN_RIGHT);
}

}  // namespace epd
