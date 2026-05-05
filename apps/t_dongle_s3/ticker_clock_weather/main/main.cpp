// T-Dongle S3: static clock + weather from Open-Meteo (no marquee).
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

char g_lastFace[160];  // "l1|l2|l3" — redraw only when this changes

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

void shortenLineToFit(char *line, int maxPx) {
    tft.setTextFont(2);
    while (strlen(line) > 1 && tft.textWidth(line) > maxPx) {
        line[strlen(line) - 1] = '\0';
    }
}

// Build up to three lines for the 160x80 panel (font 2). Truncates to fit width.
void buildFaceLines(char *l1, char *l2, char *l3, size_t lineLen, int maxPx) {
    l1[0] = l2[0] = l3[0] = '\0';

    if (WiFi.status() != WL_CONNECTED) {
        snprintf(l1, lineLen, "WiFi offline");
        snprintf(l2, lineLen, "Edit secrets.h");
        shortenLineToFit(l1, maxPx);
        shortenLineToFit(l2, maxPx);
        return;
    }
    if (!g_haveNtp) {
        snprintf(l1, lineLen, "Time syncing…");
        shortenLineToFit(l1, maxPx);
        return;
    }
    struct tm tm;
    if (!getLocalTime(&tm, 5)) {
        snprintf(l1, lineLen, "No clock");
        shortenLineToFit(l1, maxPx);
        return;
    }

    char tbuf[32];
    strftime(tbuf, sizeof(tbuf), "%a %d  %H:%M", &tm);

    if (!g_haveLoc) {
        snprintf(l1, lineLen, "%s", tbuf);
        snprintf(l2, lineLen, "Locating…");
        shortenLineToFit(l1, maxPx);
        shortenLineToFit(l2, maxPx);
        return;
    }

    if (!g_wxOk) {
        snprintf(l1, lineLen, "%s", tbuf);
        snprintf(l2, lineLen, "%s", g_place);
        snprintf(l3, lineLen, "Weather…");
        shortenLineToFit(l1, maxPx);
        shortenLineToFit(l2, maxPx);
        shortenLineToFit(l3, maxPx);
        return;
    }

    const char *unit =
        (strcmp(WEATHER_TEMP_UNIT, "fahrenheit") == 0) ? "F" : "C";
    snprintf(l1, lineLen, "%s", tbuf);
    snprintf(l2, lineLen, "%s", g_place);
    snprintf(l3, lineLen, "%.0f%s  %s", g_temp, unit, wxShort(g_wxCode));
    shortenLineToFit(l1, maxPx);
    shortenLineToFit(l2, maxPx);
    shortenLineToFit(l3, maxPx);
}

void drawFaceIfChanged(const char *l1, const char *l2, const char *l3) {
    char sig[160];
    snprintf(sig, sizeof(sig), "%s|%s|%s", l1, l2, l3);
    if (strcmp(sig, g_lastFace) == 0) {
        return;
    }
    strncpy(g_lastFace, sig, sizeof(g_lastFace));
    g_lastFace[sizeof(g_lastFace) - 1] = '\0';

    tft.fillScreen(TFT_BLACK);
    tft.setTextFont(2);
    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(TFT_CYAN, TFT_BLACK);
    constexpr int padX = 2;
    constexpr int y1   = 6;
    constexpr int y2   = 28;
    constexpr int y3   = 50;
    tft.drawString(l1, padX, y1);
    if (l2[0]) {
        tft.drawString(l2, padX, y2);
    }
    if (l3[0]) {
        tft.drawString(l3, padX, y3);
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
    tft.drawString("T-Dongle", 4, 8);
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

    tft.setTextFont(2);
    char l1[48], l2[48], l3[48];
    buildFaceLines(l1, l2, l3, sizeof(l1), kScrW - 4);
    drawFaceIfChanged(l1, l2, l3);
    delay(200);
}
