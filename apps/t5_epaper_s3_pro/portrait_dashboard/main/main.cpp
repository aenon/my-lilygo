// Portrait dashboard for LilyGo T5 E-Paper S3 Pro (H752-02).
//
// Renders a vertical 540x960 layout, refreshed once per minute:
//
//   +----------------------+
//   |    14:23             |        <- big clock (HH:MM)
//   |    Tuesday           |
//   |    Apr 28, 2026      |
//   +----------------------+
//   |  WIFI                |
//   |   SSID:   ...        |
//   |   IP:     ...        |
//   |   RSSI:   -57 dBm    |
//   +----------------------+
//   |  BATTERY             |
//   |   Charge:   78 %     |
//   |   Voltage:  3.92 V   |
//   |   Current: -120 mA   |
//   |   State:    discharg |
//   +----------------------+
//   |  SYSTEM              |
//   |   Uptime:  00:12:43  |
//   |   Heap:    324 KB    |
//   |   PSRAM:   8189 KB   |
//   |   Temp:    23 C      |
//   +----------------------+
//
// What it exercises:
//   - epdiy V7 driver, EPD_ROT_PORTRAIT (540 wide x 960 tall)
//   - XL9555/PCA9535 I/O expander (no GPS rail this time, but kept for ref)
//   - WiFi STA + NTP time sync
//   - BQ27220 battery fuel gauge
//
// Credentials live in secrets.h next to this file (gitignored).  You can
// also override at build time:
//   build_flags = -DWIFI_SSID=\"foo\" -DWIFI_PASSWORD=\"bar\"

#include <Arduino.h>
#include <WiFi.h>
#include <Wire.h>
#include <time.h>

#include <epdiy.h>
#include "bq27220.h"

#include "firasans_12.h"
#include "firasans_20.h"
#include "secrets.h"

namespace {

// ---------------------------------------------------------------------------
// Pin map (from H752-01 utilities.h)
// ---------------------------------------------------------------------------
constexpr uint8_t  kI2cSda = 39;
constexpr uint8_t  kI2cScl = 40;

// ---------------------------------------------------------------------------
// Refresh policy
// ---------------------------------------------------------------------------
constexpr uint32_t kRefreshPeriodMs = 60UL * 1000UL;
constexpr uint32_t kFullClearEvery  = 30;
constexpr int      kVcomMillivolts  = 1560;
constexpr int      kEpdRotation     = EPD_ROT_INVERTED_PORTRAIT;  // 540 wide, 960 tall

// ---------------------------------------------------------------------------
// NTP / time
// ---------------------------------------------------------------------------
constexpr const char *kNtpServer1 = "pool.ntp.org";
constexpr const char *kNtpServer2 = "time.google.com";

// ---------------------------------------------------------------------------
// Globals
// ---------------------------------------------------------------------------
EpdiyHighlevelState g_hl;
uint8_t            *g_fb         = nullptr;
BQ27220             g_bq;
bool                g_bqOk       = false;
uint32_t            g_renderCount = 0;
uint32_t            g_lastRenderMs = 0;
bool                g_wasConnected   = false;
bool                g_haveNtpSynced  = false;
uint32_t            g_lastReconnectAttemptMs = 0;

// ---------------------------------------------------------------------------
// Bring-up
// ---------------------------------------------------------------------------
void initEpd() {
    epd_init(&epd_board_v7, &ED047TC1, EPD_LUT_64K);
    epd_set_vcom(kVcomMillivolts);
    g_hl  = epd_hl_init(EPD_BUILTIN_WAVEFORM);
    g_fb  = epd_hl_get_framebuffer(&g_hl);
    epd_set_rotation((EpdRotation)kEpdRotation);

    epd_poweron();
    epd_clear();
    epd_poweroff();

    Serial.printf("[epd] %dx%d ready (portrait)\n",
                  epd_rotated_display_width(),
                  epd_rotated_display_height());
}

void initBattery() {
    // Wire.begin() must already have been called by setup() — see comment
    // there for why ordering matters with epdiy.
    g_bqOk = g_bq.init();
    Serial.printf("[bat] BQ27220 init %s\n", g_bqOk ? "ok" : "FAILED");
}

// Try to sync NTP with a short blocking window.  Idempotent — safe to call
// every time we (re)gain WiFi.
void trySyncNtp(uint32_t timeout_ms) {
    if (WiFi.status() != WL_CONNECTED) return;

    configTzTime(TZ_INFO, kNtpServer1, kNtpServer2);
    Serial.print("[time] syncing with NTP ");
    struct tm tm;
    uint32_t start = millis();
    while (!getLocalTime(&tm, 100) && millis() - start < timeout_ms) {
        Serial.print(".");
    }
    Serial.println();
    if (getLocalTime(&tm)) {
        char buf[64];
        strftime(buf, sizeof(buf), "%a %Y-%m-%d %H:%M:%S %Z", &tm);
        Serial.printf("[time] %s\n", buf);
        g_haveNtpSynced = true;
    } else {
        Serial.println("[time] sync FAILED  (will retry next time WiFi reconnects)");
    }
}

void initWifi() {
    Serial.printf("[wifi] connecting to '%s' ", WIFI_SSID);
    WiFi.mode(WIFI_STA);
    WiFi.persistent(true);          // store creds in NVS
    WiFi.setAutoReconnect(true);    // keep retrying in the background forever
    WiFi.setSleep(false);           // disable modem sleep — better latency for our once-a-min model
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    uint32_t start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < 20000) {
        delay(250);
        Serial.print(".");
    }
    Serial.println();

    if (WiFi.status() == WL_CONNECTED) {
        Serial.printf("[wifi] OK  ip=%s  rssi=%d dBm\n",
                      WiFi.localIP().toString().c_str(),
                      WiFi.RSSI());
        g_wasConnected = true;
    } else {
        Serial.println("[wifi] not yet connected — will keep retrying in the background");
    }
}

// Called from loop() before each render.  Detects edge transitions in WiFi
// state, kicks reconnect attempts, and re-syncs NTP whenever we go from
// disconnected to connected.  Non-blocking: never spends more than a few
// hundred ms in here.
void maintainWifi() {
    bool connected = (WiFi.status() == WL_CONNECTED);

    if (connected && !g_wasConnected) {
        Serial.printf("[wifi] reconnected  ip=%s  rssi=%d dBm\n",
                      WiFi.localIP().toString().c_str(),
                      WiFi.RSSI());
        g_wasConnected = true;
        // Re-sync time — we may have been offline through a DST change or a
        // long power-down; cheap to redo, big upside if our clock had drifted.
        trySyncNtp(5000);
    } else if (!connected && g_wasConnected) {
        Serial.printf("[wifi] disconnected (status=%d)\n", WiFi.status());
        g_wasConnected = false;
    }

    if (!connected) {
        // Backoff: try a manual reconnect every 30 s.  setAutoReconnect()
        // handles most cases for us, but we kick it explicitly in case the
        // SDK's internal retry has given up.
        uint32_t now = millis();
        if (now - g_lastReconnectAttemptMs > 30000) {
            g_lastReconnectAttemptMs = now;
            Serial.println("[wifi] reconnect attempt");
            WiFi.reconnect();
        }
    }

    if (connected && !g_haveNtpSynced) {
        trySyncNtp(3000);
    }
}

// ---------------------------------------------------------------------------
// Drawing helpers
// ---------------------------------------------------------------------------
constexpr uint8_t kColorWhite = 0xFF;
constexpr uint8_t kColorBlack = 0x00;

void drawText(const EpdFont *font, int x, int y, const char *s,
              EpdFontFlags flags = (EpdFontFlags)0) {
    int cx = x;
    int cy = y;
    EpdFontProperties props = epd_font_properties_default();
    props.flags = flags;
    epd_write_string(font, s, &cx, &cy, g_fb, &props);
}

void drawCenteredText(const EpdFont *font, int yBaseline, int xCenter,
                      const char *s) {
    drawText(font, xCenter, yBaseline, s, EPD_DRAW_ALIGN_CENTER);
}

void drawHLine(int x, int y, int w, uint8_t color = kColorBlack) {
    EpdRect r = {.x = x, .y = y, .width = w, .height = 1};
    epd_fill_rect(r, color, g_fb);
}

void drawSectionHeader(int y, const char *title) {
    drawText(&FiraSans_12, 36, y, title);
    drawHLine(28, y + 8, 480);
}

// ---------------------------------------------------------------------------
// Dashboard rendering
// ---------------------------------------------------------------------------
void renderDashboard() {
    g_renderCount++;
    const int W = epd_rotated_display_width();
    const int temp = epd_ambient_temperature();
    const bool fullClear = (g_renderCount % kFullClearEvery) == 1;

    if (fullClear) {
        epd_poweron();
        epd_clear();
        epd_poweroff();
    }
    epd_hl_set_all_white(&g_hl);

    char buf[96];

    // ============================================================ TIME / DATE
    struct tm tm;
    bool haveTime = WiFi.status() == WL_CONNECTED && getLocalTime(&tm);

    if (haveTime) {
        strftime(buf, sizeof(buf), "%H:%M", &tm);
    } else {
        snprintf(buf, sizeof(buf), "--:--");
    }
    drawCenteredText(&FiraSans_20, 90, W / 2, buf);
    drawCenteredText(&FiraSans_20, 130, W / 2, buf);  // double-print = pseudo-bold

    if (haveTime) {
        strftime(buf, sizeof(buf), "%A", &tm);
        drawCenteredText(&FiraSans_20, 180, W / 2, buf);
        strftime(buf, sizeof(buf), "%b %d, %Y", &tm);
        drawCenteredText(&FiraSans_20, 220, W / 2, buf);
    } else {
        drawCenteredText(&FiraSans_12, 180, W / 2, "(no time — WiFi/NTP not synced)");
    }

    drawHLine(28, 260, W - 56);

    // ============================================================ WIFI
    drawSectionHeader(300, "WIFI");
    if (WiFi.status() == WL_CONNECTED) {
        snprintf(buf, sizeof(buf), "SSID:    %s", WiFi.SSID().c_str());
        drawText(&FiraSans_12, 48, 340, buf);
        snprintf(buf, sizeof(buf), "IP:      %s", WiFi.localIP().toString().c_str());
        drawText(&FiraSans_12, 48, 370, buf);
        snprintf(buf, sizeof(buf), "RSSI:    %d dBm", WiFi.RSSI());
        drawText(&FiraSans_12, 48, 400, buf);
        snprintf(buf, sizeof(buf), "MAC:     %s", WiFi.macAddress().c_str());
        drawText(&FiraSans_12, 48, 430, buf);
    } else {
        drawText(&FiraSans_12, 48, 340, "Not connected");
    }

    // ============================================================ BATTERY
    drawSectionHeader(490, "BATTERY");
    if (g_bqOk) {
        uint16_t soc = g_bq.getStateOfCharge();
        uint16_t mv  = g_bq.getVoltage();
        int16_t  ma  = g_bq.getCurrent();
        bool    chg  = g_bq.getIsCharging();
        uint16_t soh = g_bq.getStateOfHealth();
        uint16_t cap = g_bq.getRemainingCapacity();
        uint16_t fc  = g_bq.getFullChargeCapacity();

        snprintf(buf, sizeof(buf), "Charge:    %u %%   (%u / %u mAh)", soc, cap, fc);
        drawText(&FiraSans_12, 48, 530, buf);
        snprintf(buf, sizeof(buf), "Voltage:   %.2f V", mv / 1000.0f);
        drawText(&FiraSans_12, 48, 560, buf);
        snprintf(buf, sizeof(buf), "Current:   %d mA", ma);
        drawText(&FiraSans_12, 48, 590, buf);
        snprintf(buf, sizeof(buf), "State:     %s   (health %u %%)",
                 chg ? "charging" : "discharging", soh);
        drawText(&FiraSans_12, 48, 620, buf);

        // Visual SoC bar
        EpdRect bar = {.x = 48, .y = 650, .width = (int)(W - 96),
                       .height = 24};
        epd_draw_rect(bar, kColorBlack, g_fb);
        EpdRect fill = {.x = 50, .y = 652,
                        .width = (int)((W - 100) * soc / 100),
                        .height = 20};
        epd_fill_rect(fill, kColorBlack, g_fb);
    } else {
        drawText(&FiraSans_12, 48, 530, "BQ27220 not available");
    }

    // ============================================================ SYSTEM
    drawSectionHeader(720, "SYSTEM");

    uint32_t up = millis() / 1000;
    uint32_t hh = up / 3600, mm = (up / 60) % 60, ss = up % 60;
    snprintf(buf, sizeof(buf), "Uptime:    %02lu:%02lu:%02lu",
             (unsigned long)hh, (unsigned long)mm, (unsigned long)ss);
    drawText(&FiraSans_12, 48, 760, buf);

    snprintf(buf, sizeof(buf), "Heap free: %u KB", ESP.getFreeHeap() / 1024U);
    drawText(&FiraSans_12, 48, 790, buf);

    snprintf(buf, sizeof(buf), "PSRAM free: %u / %u KB",
             ESP.getFreePsram() / 1024U, ESP.getPsramSize() / 1024U);
    drawText(&FiraSans_12, 48, 820, buf);

    snprintf(buf, sizeof(buf), "EPD temp:  %d C", temp);
    drawText(&FiraSans_12, 48, 850, buf);

    snprintf(buf, sizeof(buf), "Refreshes: %lu",
             (unsigned long)g_renderCount);
    drawText(&FiraSans_12, 48, 880, buf);

    // ============================================================ FOOTER
    drawHLine(28, 920, W - 56);
    const char *wifiTag =
        WiFi.status() == WL_CONNECTED ? "wifi:OK"
        : g_wasConnected               ? "wifi:RECONNECTING"
                                       : "wifi:DOWN";
    if (haveTime) {
        char timeBuf[32];
        strftime(timeBuf, sizeof(timeBuf), "%H:%M:%S", &tm);
        snprintf(buf, sizeof(buf), "Refreshed %s   %s   ntp:%s",
                 timeBuf, wifiTag, g_haveNtpSynced ? "OK" : "PEND");
    } else {
        snprintf(buf, sizeof(buf), "Refreshed +%lus   %s   ntp:PEND",
                 (unsigned long)up, wifiTag);
    }
    drawCenteredText(&FiraSans_12, 945, W / 2, buf);

    // ============================================================ PUSH
    epd_poweron();
    epd_hl_update_screen(&g_hl, MODE_GC16, temp);
    epd_poweroff();

    Serial.printf("[render #%lu] wifi=%d ntp=%d soc=%s heap=%u\n",
                  (unsigned long)g_renderCount,
                  WiFi.status() == WL_CONNECTED,
                  haveTime,
                  g_bqOk ? String(g_bq.getStateOfCharge()).c_str() : "n/a",
                  ESP.getFreeHeap() / 1024U);
}

}  // namespace

void setup() {
    Serial.begin(115200);
    for (uint32_t start = millis(); !Serial && millis() - start < 3000;) {
        delay(50);
    }
    Serial.println();
    Serial.println("=== T5 E-Paper S3 Pro - Portrait Dashboard ===");

    // ORDER MATTERS:
    //   1. Wire.begin() first.  epdiy v7's epd_init() installs the ESP-IDF
    //      I2C driver internally; if Wire isn't already up at that point,
    //      our later Wire.begin() will collide and leave Wire's TX buffer
    //      NULL — which silently breaks every subsequent BQ27220 / XL9555
    //      transaction.  See vendor's display_test for the same pattern.
    //   2. EPD init (ED047TC1 + TPS65185 wake).
    //   3. BQ27220 fuel gauge over Wire (which is now safely shared).
    //   4. WiFi + NTP.
    Wire.begin(kI2cSda, kI2cScl);

    initEpd();
    initBattery();
    initWifi();
    trySyncNtp(10000);

    renderDashboard();
    g_lastRenderMs = millis();
}

void loop() {
    maintainWifi();

    if (millis() - g_lastRenderMs >= kRefreshPeriodMs) {
        renderDashboard();
        g_lastRenderMs = millis();
    }
    delay(100);  // yield to FreeRTOS so the IDLE task can feed the watchdog
}
