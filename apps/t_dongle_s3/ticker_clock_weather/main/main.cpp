// T-Dongle S3: horizontal scrolling ticker with local time + Open-Meteo weather.
// Data: ip-api.com (lat/lon + city) and api.open-meteo.com (current only) — no API keys.
//
// Display: ST7735 160x80 (landscape, TFT_eSPI + LilyGO vendor Setup47).

#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <cstring>
#include <time.h>

#include <TFT_eSPI.h>

#include "secrets.h"

namespace {

constexpr int kScrW           = 160;
constexpr int kScrH           = 80;
constexpr uint8_t kBacklightPin = 38;
constexpr uint32_t kWxRetryMs = 60UL * 1000UL;
constexpr uint32_t kWxOkMs    = 60UL * 60UL * 1000UL;  // no PSRAM: refresh hourly is enough

constexpr char kNtp1[]        = "pool.ntp.org";
constexpr char kNtp2[]        = "time.google.com";

TFT_eSPI tft;

bool     g_haveNtp  = false;
bool     g_wasWifi  = false;
bool     g_haveLoc  = false;
float    g_lat      = 0;
float    g_lon      = 0;
char     g_place[48];

bool     g_wxOk     = false;
float    g_temp     = 0;
int      g_wxCode   = -1;
uint32_t g_nextWxMs = 0;

char     g_ticker[192];

struct WxLabel {
    int         code;
    const char *shortLabel;
};

const WxLabel kWx[] = {
    {0,  "Clear"}, {1,  "Mostly clr"}, {2,  "Partly cldy"}, {3,  "Overcast"},
    {45, "Fog"}, {48, "Fog"}, {51, "Drizzle"}, {53, "Drizzle"}, {55, "Drizzle"},
    {61, "Rain"}, {63, "Rain"}, {65, "Heavy rain"}, {71, "Snow"}, {73, "Snow"},
    {75, "Snow"}, {80, "Showers"}, {81, "Showers"}, {95, "T-storm"}, {96, "T-storm"},
    {99, "T-storm"},
};

const char *wxShort(int code) {
    for (const auto &w : kWx) {
        if (w.code == code) return w.shortLabel;
    }
    return "?";
}

void backlightOn() {
    pinMode(kBacklightPin, OUTPUT);
    digitalWrite(kBacklightPin, LOW);
}

void buildTicker(char *out, size_t len) {
    if (WiFi.status() != WL_CONNECTED) {
        snprintf(out, len, "  WiFi offline — check secrets.h  ");
        return;
    }
    if (!g_haveNtp) {
        snprintf(out, len, "  Time syncing…  ");
        return;
    }
    struct tm tm;
    if (!getLocalTime(&tm, 5)) {
        snprintf(out, len, "  No clock  ");
        return;
    }
    char tbuf[32];
    strftime(tbuf, sizeof(tbuf), "%a %b %d  %H:%M", &tm);

    if (!g_haveLoc) {
        snprintf(out, len, "  %s  — locating…  ", tbuf);
        return;
    }

    if (!g_wxOk) {
        snprintf(out, len, "  %s  |  %s  |  weather…  ", tbuf, g_place);
        return;
    }

    const char *unit =
        (strcmp(WEATHER_TEMP_UNIT, "fahrenheit") == 0) ? "F" : "C";
    snprintf(out, len,
             "  %s  |  %s  %.0f%s  %s  ",
             g_place, tbuf, g_temp, unit, wxShort(g_wxCode));
}

void drawMarquee(const char *text, int y) {
    tft.setTextFont(2);
    tft.setTextDatum(TL_DATUM);
    int fh = tft.fontHeight(2);
    int tw = tft.textWidth(text);
    const int gap = 36;
    int stride = tw + gap;
    if (stride < kScrW / 4) {
        stride = kScrW / 4;
    }
    int scroll = static_cast<int>((millis() / 22) % stride);

    tft.fillRect(0, y, kScrW, fh, TFT_BLACK);
    tft.setTextColor(TFT_CYAN, TFT_BLACK);

    for (int x = -scroll; x < kScrW + tw; x += stride) {
        tft.drawString(text, x, y);
    }
}

bool geolocateByIp() {
#ifdef WEATHER_OVERRIDE_LOCATION
    g_lat = WEATHER_LAT;
    g_lon = WEATHER_LON;
    snprintf(g_place, sizeof(g_place), "%s", WEATHER_LOCATION_NAME);
    g_haveLoc = true;
    return true;
#else
    if (WiFi.status() != WL_CONNECTED) return false;

    HTTPClient http;
    http.setTimeout(8000);
    http.begin("http://ip-api.com/json/?fields=status,regionName,city,lat,lon");
    int code = http.GET();
    if (code != 200) {
        http.end();
        return false;
    }
    String body = http.getString();
    http.end();
    JsonDocument doc;
    if (deserializeJson(doc, body)) {
        return false;
    }
    if (String(doc["status"] | "") != "success") return false;

    g_lat = doc["lat"] | 0.0f;
    g_lon = doc["lon"] | 0.0f;
    const char *city   = doc["city"] | "";
    const char *region = doc["regionName"] | "";
    if (region[0]) {
        snprintf(g_place, sizeof(g_place), "%s, %s", city, region);
    } else {
        snprintf(g_place, sizeof(g_place), "%s", city);
    }
    g_haveLoc = true;
    return true;
#endif
}

bool fetchWeatherCurrent() {
    if (WiFi.status() != WL_CONNECTED || !g_haveLoc) return false;

    char url[256];
    snprintf(url, sizeof(url),
             "http://api.open-meteo.com/v1/forecast?"
             "latitude=%.4f&longitude=%.4f"
             "&current=temperature_2m,weather_code"
             "&timezone=auto"
             "&temperature_unit=%s",
             g_lat, g_lon, WEATHER_TEMP_UNIT);

    HTTPClient http;
    http.setTimeout(12000);
    http.begin(url);
    int code = http.GET();
    if (code != 200) {
        http.end();
        return false;
    }
    String body = http.getString();
    http.end();
    if (body.length() == 0) return false;

    JsonDocument doc;
    if (deserializeJson(doc, body)) return false;
    JsonObject c = doc["current"];
    if (c.isNull()) return false;

    g_temp   = c["temperature_2m"] | 0.0f;
    g_wxCode = c["weather_code"] | -1;
    g_wxOk   = true;
    return true;
}

void syncNtpIfNeeded() {
    if (WiFi.status() != WL_CONNECTED || g_haveNtp) return;
    configTzTime(TZ_INFO, kNtp1, kNtp2);
    struct tm tm;
    uint32_t start = millis();
    while (!getLocalTime(&tm, 50) && millis() - start < 8000) {
        delay(100);
    }
    if (getLocalTime(&tm, 0)) {
        g_haveNtp = true;
    }
}

void wifiBegin() {
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
}

void scheduleWx(bool ok) {
    g_nextWxMs = millis() + (ok ? kWxOkMs : kWxRetryMs);
}

}  // namespace

void setup() {
    Serial.begin(115200);
    delay(300);

    tft.init();
    tft.setRotation(1);
    tft.fillScreen(TFT_BLACK);
    backlightOn();

    tft.setTextColor(TFT_GREEN, TFT_BLACK);
    tft.setTextFont(2);
    tft.drawString("T-Dongle ticker", 4, 8);
    tft.drawString("WiFi…", 4, 28);

    wifiBegin();
    uint32_t t0 = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - t0 < 25000) {
        delay(200);
    }
    tft.fillRect(0, 0, kScrW, kScrH, TFT_BLACK);

    if (WiFi.status() == WL_CONNECTED) {
        g_wasWifi = true;
        syncNtpIfNeeded();
        if (geolocateByIp()) {
            if (fetchWeatherCurrent()) {
                scheduleWx(true);
            } else {
                scheduleWx(false);
            }
        } else {
            scheduleWx(false);
        }
    }
}

void loop() {
    if (WiFi.status() == WL_CONNECTED) {
        if (!g_wasWifi) {
            g_wasWifi = true;
            syncNtpIfNeeded();
        }
        syncNtpIfNeeded();
        if (!g_haveLoc) {
            geolocateByIp();
        }
        if (static_cast<int32_t>(millis() - g_nextWxMs) >= 0) {
            if (g_haveLoc && fetchWeatherCurrent()) {
                scheduleWx(true);
            } else {
                scheduleWx(false);
            }
        }
    } else {
        g_wasWifi = false;
    }

    buildTicker(g_ticker, sizeof(g_ticker));
    drawMarquee(g_ticker, (kScrH - tft.fontHeight(2)) / 2);
    delay(5);
}
