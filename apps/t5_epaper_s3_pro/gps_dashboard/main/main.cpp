// GPS / BeiDou dashboard for LilyGo T5 E-Paper S3 Pro (H752-02).
//
// Renders a once-per-minute snapshot of GNSS state on the 960x540 e-paper:
//
//   +------------------------------------------------------------+
//   | GPS / GNSS Dashboard                       uptime / refresh |
//   +------------------------------------------------------------+
//   |  Lat:  +37.422100                                          |
//   |  Lon:  -122.084100                                         |
//   |                                                            |
//   |  Alt:  42.3 m            Speed:  0.0 km/h                  |
//   |  Hdg:  117 deg           Sats:   12 (used)                 |
//   |  HDOP: 0.85              GP:9 GL:5 BD:7 GA:0  (in view)    |
//   |  UTC:  2026-04-28 06:55:11                                 |
//   |                                                            |
//   +------------------------------------------------------------+
//   | STATUS: FIX OK / age 312ms / 18234 chars from GPS          |
//   +------------------------------------------------------------+
//
// Hardware power sequencing:
//   - The GPS module (L76K or u-blox MIA-M10Q) and the SX1262 LoRa radio
//     share a 3V3 rail that is gated by IO0 of the on-board XL9555/PCA9535
//     I/O expander.  We must drive that pin HIGH at boot to power the GNSS
//     module up, otherwise UART1 will see noise / nothing.
//   - The TPS65185 e-paper PMIC is driven internally by epdiy's "v7" board
//     variant; we don't have to talk to the expander for that.
//
// References:
//   examples/GPS/main/main.ino           — vendor's GPS auto-detect / setup
//   examples/display_test/main/main.cpp  — vendor's epdiy + ED047TC1 init

#include <Arduino.h>
#include <Wire.h>

#include <epdiy.h>
#include <TinyGPS++.h>
#include "ExtensionIOXL9555.hpp"

#include "firasans_12.h"
#include "firasans_20.h"

namespace {

// ---------------------------------------------------------------------------
// Pin map  (from H752-01 utilities.h, see vendor README)
// ---------------------------------------------------------------------------
constexpr uint8_t  kI2cSda      = 39;
constexpr uint8_t  kI2cScl      = 40;
constexpr uint8_t  kGpsRx       = 44;   // ESP32 RX  <-  GPS TX
constexpr uint8_t  kGpsTx       = 43;   // ESP32 TX  ->  GPS RX
constexpr uint32_t kGpsBaudL76K = 9600;

// ---------------------------------------------------------------------------
// Display + refresh policy
// ---------------------------------------------------------------------------
constexpr uint32_t kRefreshPeriodMs   = 60UL * 1000UL;   // once per minute
constexpr uint32_t kFullClearEvery    = 30;              // full clear every 30 frames
constexpr int      kVcomMillivolts    = 1560;            // matches display_test
constexpr int      kEpdRotation       = EPD_ROT_LANDSCAPE;  // 960x540

// ---------------------------------------------------------------------------
// Globals
// ---------------------------------------------------------------------------
EpdiyHighlevelState g_hl;
uint8_t            *g_fb         = nullptr;
ExtensionIOXL9555   g_io;
TinyGPSPlus         g_gps;

uint32_t g_lastRenderMs = 0;
uint32_t g_renderCount  = 0;

// Per-constellation satellites-in-view, parsed from raw $..GSV messages.
TinyGPSCustom g_satsGp(g_gps, "GPGSV", 3);  // GPS
TinyGPSCustom g_satsGl(g_gps, "GLGSV", 3);  // GLONASS
TinyGPSCustom g_satsBd(g_gps, "BDGSV", 3);  // BeiDou (legacy talker ID)
TinyGPSCustom g_satsGb(g_gps, "GBGSV", 3);  // BeiDou (newer talker ID)
TinyGPSCustom g_satsGa(g_gps, "GAGSV", 3);  // Galileo

// ---------------------------------------------------------------------------
// Bring-up helpers
// ---------------------------------------------------------------------------
void initIoExpander() {
    if (!g_io.init(Wire, kI2cSda, kI2cScl, XL9555_SLAVE_ADDRESS0)) {
        Serial.println("FATAL: XL9555/PCA9535 not found on I2C 0x20");
        while (true) {
            delay(1000);
        }
    }
    // IO0 gates the shared LoRa+GPS 3V3 rail.  Drive it HIGH to power them up.
    g_io.pinMode(ExtensionIOXL9555::IO0, OUTPUT);
    g_io.digitalWrite(ExtensionIOXL9555::IO0, HIGH);
    delay(500);
    Serial.println("[io] XL9555 ok, GPS+LoRa power rail enabled");
}

void initEpd() {
    epd_init(&epd_board_v7, &ED047TC1, EPD_LUT_64K);
    epd_set_vcom(kVcomMillivolts);
    g_hl  = epd_hl_init(EPD_BUILTIN_WAVEFORM);
    g_fb  = epd_hl_get_framebuffer(&g_hl);
    epd_set_rotation((EpdRotation)kEpdRotation);

    epd_poweron();
    epd_clear();
    epd_poweroff();

    Serial.printf("[epd] %dx%d ready, vcom=%d mV\n",
                  epd_rotated_display_width(), epd_rotated_display_height(),
                  kVcomMillivolts);
}

void configureL76K() {
    Serial1.begin(kGpsBaudL76K, SERIAL_8N1, kGpsRx, kGpsTx);
    delay(200);

    // Enable GPS + GLONASS + BeiDou on the L76K (constellation mask = 0b0111)
    Serial1.write("$PCAS04,7*1E\r\n");
    delay(200);

    // Enable RMC, VTG, GGA, GSA, GSV, GLL, ZDA NMEA outputs at 1 Hz
    Serial1.write("$PCAS03,1,1,1,1,1,1,1,1,1,1,,,0,0*02\r\n");
    delay(200);

    // Vehicle dynamic mode (better for moving targets)
    Serial1.write("$PCAS11,3*1E\r\n");
    delay(200);

    Serial.println("[gps] L76K configured: GPS+GLONASS+BeiDou @ 9600 baud");
}

// ---------------------------------------------------------------------------
// Drawing primitives
// ---------------------------------------------------------------------------
constexpr uint8_t kColorWhite     = 0xFF;
constexpr uint8_t kColorLightGray = 0xC0;
constexpr uint8_t kColorMidGray   = 0x80;
constexpr uint8_t kColorBlack     = 0x00;

void drawText(const EpdFont *font, int x, int y, const char *s) {
    int cx = x;
    int cy = y;
    EpdFontProperties props = epd_font_properties_default();
    epd_write_string(font, s, &cx, &cy, g_fb, &props);
}

void drawHLine(int x, int y, int w, uint8_t color = kColorBlack) {
    EpdRect r = {.x = x, .y = y, .width = w, .height = 1};
    epd_fill_rect(r, color, g_fb);
}

// ---------------------------------------------------------------------------
// Dashboard rendering
// ---------------------------------------------------------------------------
void renderDashboard() {
    g_renderCount++;
    const int W = epd_rotated_display_width();
    const int H = epd_rotated_display_height();
    const int temp = epd_ambient_temperature();
    const bool fullClear = (g_renderCount % kFullClearEvery) == 1;

    if (fullClear) {
        epd_poweron();
        epd_clear();
        epd_poweroff();
    }

    epd_hl_set_all_white(&g_hl);

    // ---- Title bar -------------------------------------------------------
    drawText(&FiraSans_20, 24, 38, "GPS / GNSS Dashboard");

    char buf[96];
    uint32_t up = millis() / 1000;
    snprintf(buf, sizeof(buf), "uptime %lus  refresh #%lu",
             static_cast<unsigned long>(up),
             static_cast<unsigned long>(g_renderCount));
    drawText(&FiraSans_12, W - 280, 36, buf);

    drawHLine(20, 56, W - 40);

    // ---- Lat / Lon (the headline numbers) --------------------------------
    if (g_gps.location.isValid()) {
        snprintf(buf, sizeof(buf), "Lat:  %+.6f", g_gps.location.lat());
    } else {
        snprintf(buf, sizeof(buf), "Lat:  --");
    }
    drawText(&FiraSans_20, 30, 110, buf);

    if (g_gps.location.isValid()) {
        snprintf(buf, sizeof(buf), "Lon:  %+.6f", g_gps.location.lng());
    } else {
        snprintf(buf, sizeof(buf), "Lon:  --");
    }
    drawText(&FiraSans_20, 30, 150, buf);

    // ---- Two-column readout ---------------------------------------------
    const int kColLeft  = 30;
    const int kColRight = 500;

    if (g_gps.altitude.isValid())
        snprintf(buf, sizeof(buf), "Alt:    %.1f m", g_gps.altitude.meters());
    else
        snprintf(buf, sizeof(buf), "Alt:    --");
    drawText(&FiraSans_20, kColLeft, 220, buf);

    if (g_gps.speed.isValid())
        snprintf(buf, sizeof(buf), "Speed:  %.1f km/h", g_gps.speed.kmph());
    else
        snprintf(buf, sizeof(buf), "Speed:  --");
    drawText(&FiraSans_20, kColRight, 220, buf);

    if (g_gps.course.isValid())
        snprintf(buf, sizeof(buf), "Hdg:    %.0f deg", g_gps.course.deg());
    else
        snprintf(buf, sizeof(buf), "Hdg:    --");
    drawText(&FiraSans_20, kColLeft, 270, buf);

    if (g_gps.satellites.isValid())
        snprintf(buf, sizeof(buf), "Sats:   %d (used)",
                 static_cast<int>(g_gps.satellites.value()));
    else
        snprintf(buf, sizeof(buf), "Sats:   --");
    drawText(&FiraSans_20, kColRight, 270, buf);

    if (g_gps.hdop.isValid())
        snprintf(buf, sizeof(buf), "HDOP:   %.2f", g_gps.hdop.hdop());
    else
        snprintf(buf, sizeof(buf), "HDOP:   --");
    drawText(&FiraSans_20, kColLeft, 320, buf);

    int gp = atoi(g_satsGp.value());
    int gl = atoi(g_satsGl.value());
    int bd = atoi(g_satsBd.value()) + atoi(g_satsGb.value());
    int ga = atoi(g_satsGa.value());
    snprintf(buf, sizeof(buf), "GP:%d  GL:%d  BD:%d  GA:%d  (in view)",
             gp, gl, bd, ga);
    drawText(&FiraSans_20, kColRight, 320, buf);

    if (g_gps.date.isValid() && g_gps.time.isValid()) {
        snprintf(buf, sizeof(buf), "UTC:    %04u-%02u-%02u  %02u:%02u:%02u",
                 g_gps.date.year(), g_gps.date.month(), g_gps.date.day(),
                 g_gps.time.hour(), g_gps.time.minute(), g_gps.time.second());
    } else {
        snprintf(buf, sizeof(buf), "UTC:    --");
    }
    drawText(&FiraSans_20, kColLeft, 380, buf);

    // ---- Status footer ---------------------------------------------------
    drawHLine(20, H - 50, W - 40);

    if (g_gps.charsProcessed() < 10) {
        snprintf(buf, sizeof(buf),
                 "STATUS: no NMEA from GPS - check antenna / power rail");
    } else if (!g_gps.location.isValid()) {
        snprintf(buf, sizeof(buf),
                 "STATUS: searching - %lu chars, %lu sentences w/ fix",
                 g_gps.charsProcessed(),
                 g_gps.sentencesWithFix());
    } else {
        uint32_t ageMs = g_gps.location.age();
        snprintf(buf, sizeof(buf),
                 "STATUS: FIX OK  -  age %lu ms  -  %lu chars from GPS",
                 ageMs, g_gps.charsProcessed());
    }
    drawText(&FiraSans_12, 24, H - 22, buf);

    // ---- Push to panel ---------------------------------------------------
    epd_poweron();
    epd_hl_update_screen(&g_hl, MODE_GC16, temp);
    epd_poweroff();

    Serial.printf("[render #%lu] valid=%d sats=%d chars=%lu temp=%dC\n",
                  static_cast<unsigned long>(g_renderCount),
                  g_gps.location.isValid() ? 1 : 0,
                  g_gps.satellites.isValid() ? g_gps.satellites.value() : 0,
                  g_gps.charsProcessed(),
                  temp);
}

}  // namespace

// ---------------------------------------------------------------------------
// Arduino entry points
// ---------------------------------------------------------------------------
void setup() {
    Serial.begin(115200);
    for (uint32_t start = millis(); !Serial && millis() - start < 3000;) {
        delay(50);
    }
    Serial.println();
    Serial.println("=== T5 E-Paper S3 Pro - GPS Dashboard ===");

    // Note: don't call Wire.begin() here — XL9555::init() does it internally,
    // and epd_init() also wants to install the I2C driver itself.  Calling it
    // up front causes the harmless but noisy "Bus already started" / "i2c
    // driver install error" messages.

    initIoExpander();
    initEpd();
    configureL76K();

    renderDashboard();
    g_lastRenderMs = millis();
}

void loop() {
    while (Serial1.available()) {
        g_gps.encode(Serial1.read());
    }

    if (millis() - g_lastRenderMs >= kRefreshPeriodMs) {
        renderDashboard();
        g_lastRenderMs = millis();
    }

    // Yield to FreeRTOS so the IDLE task on CPU 0 can run and feed the task
    // watchdog.  Without this, the Arduino loopTask (priority 1) starves
    // IDLE (priority 0) and the watchdog aborts after ~5 s.
    delay(10);
}
