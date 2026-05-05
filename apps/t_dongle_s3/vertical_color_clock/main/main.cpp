// T-Dongle S3: portrait clock — stacked face (22: / 52 / Thu / Apr / 30), HSV drift.
// WiFi + NTP at boot (TZ from secrets.h).
//
// Physical habit: **USB-A toward the bottom** (default `kTftRotation`).
// See kTftRotation (ST7735 / TFT_eSPI + LilyGO vendor Setup47).

#include <Arduino.h>
#include <WiFi.h>
#include <cmath>
#include <cstring>
#include <time.h>

#include <TFT_eSPI.h>

#include "secrets.h"

namespace {

// Portrait logical size is 80 (W) x 160 (H). Default rotation **0** = upright
// clock with **USB-A toward the bottom** on T-Dongle-S3. Use **2** if yours is
// flipped (USB ends up at the top).
constexpr uint8_t kTftRotation = 0;
constexpr uint8_t kBacklightPin = 38;

constexpr char kNtp1[] = "pool.ntp.org";
constexpr char kNtp2[] = "time.google.com";

// Full hue cycle duration (ms). Larger = slower color drift.
constexpr uint32_t kHueCycleMs = 240000;

// Minimum interval between repaints for color animation alone.
constexpr uint32_t kColorFrameMs = 350;

TFT_eSPI tft;

bool g_haveNtp = false;

uint16_t hsvTo565(float h, float s, float v) {
    while (h >= 360.0f) {
        h -= 360.0f;
    }
    while (h < 0.0f) {
        h += 360.0f;
    }
    float c  = v * s;
    float hp = h / 60.0f;
    float x  = c * (1.0f - fabsf(fmodf(hp, 2.0f) - 1.0f));
    float m  = v - c;
    float r1, g1, b1;
    int   sector = (int)floorf(hp);
    if (sector < 0) {
        sector = 0;
    }
    if (sector >= 6) {
        sector = 5;
    }
    switch (sector) {
    case 0:
        r1 = c;
        g1 = x;
        b1 = 0;
        break;
    case 1:
        r1 = x;
        g1 = c;
        b1 = 0;
        break;
    case 2:
        r1 = 0;
        g1 = c;
        b1 = x;
        break;
    case 3:
        r1 = 0;
        g1 = x;
        b1 = c;
        break;
    case 4:
        r1 = x;
        g1 = 0;
        b1 = c;
        break;
    default:
        r1 = c;
        g1 = 0;
        b1 = x;
        break;
    }
    auto ch = [](float f, float mm) {
        int n = (int)((f + mm) * 255.0f);
        if (n < 0) {
            return (uint8_t)0;
        }
        if (n > 255) {
            return (uint8_t)255;
        }
        return (uint8_t)n;
    };
    return tft.color565(ch(r1, m), ch(g1, m), ch(b1, m));
}

void backlightOn() {
    pinMode(kBacklightPin, OUTPUT);
    digitalWrite(kBacklightPin, LOW);
}

void syncNtpBlocking() {
    configTzTime(TZ_INFO, kNtp1, kNtp2);
    struct tm tm;
    uint32_t start = millis();
    while (!getLocalTime(&tm, 80) && millis() - start < 15000) {
        delay(150);
    }
    g_haveNtp = getLocalTime(&tm, 0);
}

// Biggest font for both HH: and MM that fits 80px width and leaves vertical
// room for three date lines (font 2). Try in order: 6 → 7 → 4 → 2×2 → 2.
void pickLargestTimeFont(const char *hourLine, const char *minLine, uint8_t *outFont,
                        uint8_t *outSize) {
    const int maxW = tft.width() - 4;
    constexpr int padTop   = 2;
    constexpr int gapHm    = 2;
    constexpr int gapDates = 6;
    constexpr int gapD     = 2;

    static const struct {
        uint8_t font;
        uint8_t size;
    } cand[] = {
        {6, 1},
        {7, 1},
        {4, 1},
        {2, 2},
        {2, 1},
    };

    tft.setTextFont(2);
    tft.setTextSize(1);
    const int fhDate = tft.fontHeight();

    for (const auto &c : cand) {
        tft.setTextFont(c.font);
        tft.setTextSize(c.size);
        if (tft.textWidth(hourLine) > maxW || tft.textWidth(minLine) > maxW) {
            continue;
        }
        const int fhBig = tft.fontHeight();
        const int used  = padTop + 2 * fhBig + gapHm + gapDates + 3 * fhDate + 2 * gapD;
        if (used > tft.height() - 2) {
            continue;
        }
        *outFont = c.font;
        *outSize = c.size;
        return;
    }

    tft.setTextFont(2);
    tft.setTextSize(1);
    *outFont = 2;
    *outSize = 1;
}

// Face layout (top → bottom):
//   "22:"  "52"     — large
//   "Thu"  "Apr" "30" — small
void paintPortrait(const struct tm &tm) {
    char hourColon[8];
    char minutes[8];
    char dow[8];
    char mon[8];
    char day[8];

    snprintf(hourColon, sizeof(hourColon), "%02d:", tm.tm_hour);
    snprintf(minutes, sizeof(minutes), "%02d", tm.tm_min);
    strftime(dow, sizeof(dow), "%a", &tm);
    strftime(mon, sizeof(mon), "%b", &tm);
    snprintf(day, sizeof(day), "%d", tm.tm_mday);

    float hue = fmodf(static_cast<float>(millis() % kHueCycleMs) * (360.0f / static_cast<float>(kHueCycleMs)),
                      360.0f);
    uint16_t fgBig   = hsvTo565(hue, 0.75f, 0.95f);
    uint16_t fgSmall = hsvTo565(hue + 35.0f, 0.40f, 0.86f);

    tft.fillScreen(TFT_BLACK);

    const int cx = tft.width() / 2;

    tft.setTextDatum(MC_DATUM);

    uint8_t tFont, tSize;
    pickLargestTimeFont(hourColon, minutes, &tFont, &tSize);
    tft.setTextFont(tFont);
    tft.setTextSize(tSize);
    const int fhBig = tft.fontHeight();

    constexpr int padTop   = 2;
    constexpr int gapHm    = 2;
    constexpr int gapDates = 6;
    constexpr int gapD     = 2;

    const int yHour = padTop + fhBig / 2;
    const int yMin  = yHour + fhBig + gapHm;

    tft.setTextColor(fgBig, TFT_BLACK);
    tft.drawString(hourColon, cx, yHour);
    tft.drawString(minutes, cx, yMin);

    tft.setTextFont(2);
    tft.setTextSize(1);
    const int fhDate = tft.fontHeight();
    const int yDates = yMin + fhBig / 2 + gapDates + fhDate / 2;
    tft.setTextColor(fgSmall, TFT_BLACK);
    tft.drawString(dow, cx, yDates);
    tft.drawString(mon, cx, yDates + fhDate + gapD);
    tft.drawString(day, cx, yDates + 2 * (fhDate + gapD));
}

void faceSignature(const struct tm &tm, char *out, size_t outLen) {
    snprintf(out, outLen, "%02d:%02d|%d-%d-%d",
             tm.tm_hour, tm.tm_min,
             tm.tm_year, tm.tm_mon, tm.tm_mday);
}

void showNoTime() {
    tft.fillScreen(TFT_BLACK);
    tft.setTextFont(2);
    tft.setTextSize(1);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(TFT_RED, TFT_BLACK);
    tft.drawString("No NTP", tft.width() / 2, tft.height() / 2 - 10);
    tft.drawString("WiFi/time", tft.width() / 2, tft.height() / 2 + 10);
}

}  // namespace

void setup() {
    Serial.begin(115200);
    delay(200);

    tft.init();
    tft.setRotation(kTftRotation);
    tft.fillScreen(TFT_BLACK);
    backlightOn();

    tft.setTextFont(2);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.drawString("WiFi…", tft.width() / 2, tft.height() / 2 - 10);
    tft.drawString("clock", tft.width() / 2, tft.height() / 2 + 10);

    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    uint32_t t0 = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - t0 < 25000) {
        delay(200);
    }

    tft.fillScreen(TFT_BLACK);
    if (WiFi.status() == WL_CONNECTED) {
        syncNtpBlocking();
    }

    struct tm tm;
    if (g_haveNtp && getLocalTime(&tm, 10)) {
        paintPortrait(tm);
    } else {
        showNoTime();
    }
}

void loop() {
    static uint32_t s_lastColorPaint = 0;
    static char     s_prevSig[32]  = "";

    if (WiFi.status() == WL_CONNECTED && !g_haveNtp) {
        syncNtpBlocking();
    }

    struct tm tm;
    if (!g_haveNtp || !getLocalTime(&tm, 5)) {
        showNoTime();
        delay(800);
        return;
    }

    char sig[32];
    faceSignature(tm, sig, sizeof(sig));

    bool timeTick = (strcmp(sig, s_prevSig) != 0);
    uint32_t now  = millis();
    bool colorTick =
        (now - s_lastColorPaint) >= kColorFrameMs;

    if (timeTick) {
        strncpy(s_prevSig, sig, sizeof(s_prevSig));
        s_prevSig[sizeof(s_prevSig) - 1] = '\0';
    }

    if (timeTick || colorTick) {
        s_lastColorPaint = now;
        paintPortrait(tm);
    }

    delay(200);
}
