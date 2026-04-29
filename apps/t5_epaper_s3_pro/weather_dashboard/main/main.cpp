// Weather dashboard for LilyGo T5 E-Paper S3 Pro (H752-02).
//
// Renders a 540x960 portrait dashboard:
//
//   ┌─────────────────────────────┐
//   │ MOUNTAIN VIEW, CA           │  <- city + state from IP geolocation
//   │ Tue Apr 28        14:23     │
//   ├─────────────────────────────┤
//   │                             │
//   │           72°               │  <- big current temp
//   │       Partly Cloudy         │
//   │                             │
//   │   Feels 70°  Humid 45%      │
//   │   Wind  8 mph WSW           │
//   │   Precip 0.0 in   UV 4      │
//   ├─────────────────────────────┤
//   │ NEXT 6 HOURS                │
//   │  3p  4p  5p  6p  7p  8p     │
//   │ 72° 71° 70° 68° 66° 64°     │
//   │  Cl  Cl  Su  Su  Cl  Ra     │
//   ├─────────────────────────────┤
//   │ NEXT 5 DAYS                 │
//   │ Tue  Wed  Thu  Fri  Sat     │
//   │ 75°  73°  68°  72°  78°     │
//   │ 55°  52°  48°  56°  62°     │
//   │  Cl   Ra   Ra   Su   Su     │
//   ├─────────────────────────────┤
//   │ ↑ 06:22  ↓ 19:48            │
//   │ wifi:OK  ntp:OK  fetched .. │
//   └─────────────────────────────┘
//
// Data sources (no API keys, no signup):
//   - ip-api.com/json     -> initial latitude/longitude + city name
//   - api.open-meteo.com  -> current conditions, hourly + daily forecast
//
// Refresh policy:
//   - Screen redraw  : every 60 s (clock + footer; full weather redraw)
//   - Weather fetch  : on success -> top of next hour; on failure -> +60 s
//   - Location lookup: once per boot (until geolocate succeeds)

#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <cstring>
#include <WiFi.h>
#include <Wire.h>
#include <time.h>

#include <epdiy.h>
#include "bq27220.h"

#include "firasans_12.h"
#include "firasans_20.h"
#include "secrets.h"
#include "../../../_shared/bq27220_idf_read.h"

namespace {

// ---------------------------------------------------------------------------
// Pin map / refresh policy
// ---------------------------------------------------------------------------
constexpr uint8_t  kI2cSda            = 39;
constexpr uint8_t  kI2cScl            = 40;
constexpr uint32_t kScreenRefreshMs   = 60UL * 1000UL;     // clock cadence
constexpr uint32_t kWxRetryMs         = 60UL * 1000UL;     // after a failed fetch
constexpr uint32_t kWxFallbackOkMs    = 15UL * 60UL * 1000UL;  // success cadence when NTP missing
constexpr uint32_t kFullClearEvery    = 30;
constexpr int      kVcomMillivolts    = 1560;
constexpr int      kEpdRotation       = EPD_ROT_INVERTED_PORTRAIT;
constexpr const char *kNtpServer1     = "pool.ntp.org";
constexpr const char *kNtpServer2     = "time.google.com";
constexpr int      kHourlyCount       = 6;
constexpr int      kDailyCount        = 5;
constexpr i2c_port_t kBattI2cPort     = I2C_NUM_0;

// ---------------------------------------------------------------------------
// Data model
// ---------------------------------------------------------------------------
struct CurrentWeather {
    bool   valid           = false;
    float  temp            = 0.0f;
    float  feelsLike       = 0.0f;
    float  humidity        = 0.0f;
    float  windSpeed       = 0.0f;
    float  windDirDeg      = 0.0f;
    float  precip          = 0.0f;
    float  uvIndex         = 0.0f;
    int    weatherCode     = -1;
    bool   isDay           = true;
};

struct HourlyEntry {
    time_t epoch       = 0;
    float  temp        = 0.0f;
    int    weatherCode = -1;
    int    precipPct   = 0;
};

struct DailyEntry {
    time_t epoch        = 0;
    float  tempMax      = 0.0f;
    float  tempMin      = 0.0f;
    int    weatherCode  = -1;
    time_t sunrise      = 0;
    time_t sunset       = 0;
    float  precipSum    = 0.0f;
};

struct WeatherSnapshot {
    bool             ok               = false;
    uint32_t         fetchedMs        = 0;
    CurrentWeather   current;
    HourlyEntry      hourly[kHourlyCount];
    DailyEntry       daily[kDailyCount];
};

// ---------------------------------------------------------------------------
// Globals
// ---------------------------------------------------------------------------
EpdiyHighlevelState g_hl;
uint8_t            *g_fb         = nullptr;
BQ27220             g_bq;
bool                g_bqOk       = false;
uint32_t            g_renderCount = 0;
uint32_t            g_lastScreenMs   = 0;
uint32_t            g_lastReconnectAttemptMs = 0;

// Next weather-fetch schedule.  Two parallel representations because we may
// not have NTP-synced wall clock yet at first boot:
//   - if useEpoch, fire when time(NULL) >= nextAttemptEpoch
//   - else, fire when (millis() - nextAttemptMillis) is non-negative
struct WxSchedule {
    bool     useEpoch          = false;
    time_t   nextAttemptEpoch  = 0;
    uint32_t nextAttemptMillis = 0;
};
WxSchedule g_wxSched;
bool       g_lastFetchOk      = false;
bool                g_wasConnected   = false;
bool                g_haveNtpSynced  = false;

float               g_lat            = 0.0f;
float               g_lon            = 0.0f;
String              g_locationName;
bool                g_haveLocation   = false;

WeatherSnapshot     g_weather;

// ---------------------------------------------------------------------------
// Weather code helpers
// ---------------------------------------------------------------------------
struct WxLabel { int code; const char *label; const char *glyph; };

constexpr WxLabel kWxLabels[] = {
    {  0, "Clear",         "Su" },
    {  1, "Mostly Clear",  "Mc" },
    {  2, "Partly Cloudy", "Pc" },
    {  3, "Overcast",      "Cl" },
    { 45, "Fog",           "Fg" },
    { 48, "Rime Fog",      "Fg" },
    { 51, "Light Drizzle", "Dr" },
    { 53, "Drizzle",       "Dr" },
    { 55, "Heavy Drizzle", "Dr" },
    { 56, "Frz Drizzle",   "Fz" },
    { 57, "Frz Drizzle",   "Fz" },
    { 61, "Light Rain",    "Ra" },
    { 63, "Rain",          "Ra" },
    { 65, "Heavy Rain",    "Ra" },
    { 66, "Frz Rain",      "Fz" },
    { 67, "Frz Rain",      "Fz" },
    { 71, "Light Snow",    "Sn" },
    { 73, "Snow",          "Sn" },
    { 75, "Heavy Snow",    "Sn" },
    { 77, "Snow Grains",   "Sn" },
    { 80, "Rain Shower",   "Ra" },
    { 81, "Rain Shower",   "Ra" },
    { 82, "Heavy Showers", "Ra" },
    { 85, "Snow Shower",   "Sn" },
    { 86, "Snow Shower",   "Sn" },
    { 95, "Thunderstorm",  "Ts" },
    { 96, "Thunderstorm",  "Ts" },
    { 99, "Thunderstorm",  "Ts" },
};

const char *wxLabel(int code) {
    for (const auto &row : kWxLabels) if (row.code == code) return row.label;
    return "Unknown";
}
const char *wxGlyph(int code) {
    for (const auto &row : kWxLabels) if (row.code == code) return row.glyph;
    return "?";
}
const char *windCardinal(float deg) {
    static const char *kDirs[] = {"N","NE","E","SE","S","SW","W","NW"};
    int idx = (int)((deg + 22.5f) / 45.0f) & 7;
    return kDirs[idx];
}

// ---------------------------------------------------------------------------
// Weather fetch scheduling
//   - Success -> next attempt at the top of the next hour (HH:00:00).
//   - Failure -> retry in kWxRetryMs (60 s).
//   - In either case, the caller is expected to redraw the screen so the
//     dashboard shows the freshest state right away.
// ---------------------------------------------------------------------------
time_t computeNextHourEpoch() {
    time_t now = time(nullptr);
    struct tm tm;
    localtime_r(&now, &tm);
    tm.tm_hour += 1;
    tm.tm_min  = 0;
    tm.tm_sec  = 0;
    tm.tm_isdst = -1;  // let mktime resolve DST
    return mktime(&tm);
}

void scheduleNextWxAttempt(bool success) {
    if (g_haveNtpSynced) {
        g_wxSched.useEpoch = true;
        if (success) {
            g_wxSched.nextAttemptEpoch = computeNextHourEpoch();
        } else {
            g_wxSched.nextAttemptEpoch = time(nullptr) + (kWxRetryMs / 1000UL);
        }
        struct tm tmn;
        localtime_r(&g_wxSched.nextAttemptEpoch, &tmn);
        Serial.printf("[wx] next attempt at %02d:%02d:%02d (%s)\n",
                      tmn.tm_hour, tmn.tm_min, tmn.tm_sec,
                      success ? "success -> top of hour" : "failure -> +60s");
    } else {
        g_wxSched.useEpoch = false;
        g_wxSched.nextAttemptMillis =
            millis() + (success ? kWxFallbackOkMs : kWxRetryMs);
        Serial.printf("[wx] next attempt in %lus (%s, no NTP yet)\n",
                      success ? (kWxFallbackOkMs / 1000UL)
                              : (kWxRetryMs       / 1000UL),
                      success ? "success" : "failure");
    }
}

bool isWxFetchDue() {
    if (g_wxSched.useEpoch) {
        return time(nullptr) >= g_wxSched.nextAttemptEpoch;
    }
    return (int32_t)(millis() - g_wxSched.nextAttemptMillis) >= 0;
}

void formatNextWxAttempt(char *out, size_t outLen) {
    if (g_wxSched.useEpoch && g_haveNtpSynced) {
        struct tm tmn;
        localtime_r(&g_wxSched.nextAttemptEpoch, &tmn);
        snprintf(out, outLen, "%02d:%02d", tmn.tm_hour, tmn.tm_min);
    } else if (!g_wxSched.useEpoch) {
        int32_t left_ms = (int32_t)(g_wxSched.nextAttemptMillis - millis());
        if (left_ms < 0) left_ms = 0;
        snprintf(out, outLen, "+%lds", (long)(left_ms / 1000));
    } else {
        snprintf(out, outLen, "?");
    }
}

// ---------------------------------------------------------------------------
// EPD bring-up
// ---------------------------------------------------------------------------
// epd_hl_update_screen() applies a diff from back_fb -> front_fb.  After a
// hardware epd_clear() the panel is physically white, but back_fb still
// holds the previous image — the next diff/update chases the wrong baseline,
// which shows up as progressive ghosting and "missing" text until something
// recovers.  Reset BOTH framebuffers whenever we clear the panel.
void hlSyncFramebuffersToWhite(EpdiyHighlevelState *hl) {
    const int fbBytes = epd_width() / 2 * epd_height();
    std::memset(hl->front_fb, 0xFF, fbBytes);
    std::memset(hl->back_fb, 0xFF, fbBytes);
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

    Serial.printf("[epd] %dx%d ready (inverted portrait)\n",
                  epd_rotated_display_width(),
                  epd_rotated_display_height());
}

void initBattery() {
    g_bqOk = g_bq.init();
    Serial.printf("[bat] BQ27220 %s\n", g_bqOk ? "ok" : "FAILED");
}

// ---------------------------------------------------------------------------
// WiFi self-healing
// ---------------------------------------------------------------------------
void trySyncNtp(uint32_t timeout_ms) {
    if (WiFi.status() != WL_CONNECTED) return;

    configTzTime(TZ_INFO, kNtpServer1, kNtpServer2);
    Serial.print("[time] syncing NTP ");
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
        Serial.println("[time] sync FAILED");
    }
}

void initWifi() {
    Serial.printf("[wifi] connecting to '%s' ", WIFI_SSID);
    WiFi.mode(WIFI_STA);
    WiFi.persistent(true);
    WiFi.setAutoReconnect(true);
    WiFi.setSleep(false);
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
        Serial.println("[wifi] not yet connected — will retry in background");
    }
}

void maintainWifi() {
    bool connected = (WiFi.status() == WL_CONNECTED);
    if (connected && !g_wasConnected) {
        Serial.printf("[wifi] reconnected ip=%s rssi=%d\n",
                      WiFi.localIP().toString().c_str(), WiFi.RSSI());
        g_wasConnected = true;
        trySyncNtp(5000);
        // We were offline; let the scheduler fire as soon as we get back to
        // loop() instead of waiting out whatever retry it had scheduled.
        g_wxSched.useEpoch          = false;
        g_wxSched.nextAttemptMillis = millis();
    } else if (!connected && g_wasConnected) {
        Serial.printf("[wifi] disconnected (status=%d)\n", WiFi.status());
        g_wasConnected = false;
    }
    if (!connected) {
        uint32_t now = millis();
        if (now - g_lastReconnectAttemptMs > 30000) {
            g_lastReconnectAttemptMs = now;
            Serial.println("[wifi] manual reconnect kick");
            WiFi.reconnect();
        }
    }
    if (connected && !g_haveNtpSynced) trySyncNtp(3000);
}

// ---------------------------------------------------------------------------
// Networking helpers — IP geolocation + Open-Meteo
// ---------------------------------------------------------------------------
bool geolocateByIp() {
#ifdef WEATHER_OVERRIDE_LOCATION
    g_lat = WEATHER_LAT;
    g_lon = WEATHER_LON;
    g_locationName = String(WEATHER_LOCATION_NAME);
    g_haveLocation = true;
    Serial.printf("[loc] override -> %s  %.4f,%.4f\n",
                  g_locationName.c_str(), g_lat, g_lon);
    return true;
#else
    if (WiFi.status() != WL_CONNECTED) return false;

    HTTPClient http;
    http.setTimeout(8000);
    http.begin("http://ip-api.com/json/?fields=status,country,regionName,city,lat,lon");
    int code = http.GET();
    if (code != 200) {
        Serial.printf("[loc] ip-api HTTP %d\n", code);
        http.end();
        return false;
    }

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, http.getStream());
    http.end();
    if (err) {
        Serial.printf("[loc] json error: %s\n", err.c_str());
        return false;
    }

    if (String(doc["status"] | "") != "success") {
        Serial.printf("[loc] ip-api status != success\n");
        return false;
    }
    g_lat = doc["lat"] | 0.0f;
    g_lon = doc["lon"] | 0.0f;
    String city   = doc["city"]       | String("");
    String region = doc["regionName"] | String("");
    g_locationName = city + (region.length() ? ", " + region : "");
    g_haveLocation = true;

    Serial.printf("[loc] %s  %.4f,%.4f\n",
                  g_locationName.c_str(), g_lat, g_lon);
    return true;
#endif
}

bool fetchWeather() {
    if (WiFi.status() != WL_CONNECTED || !g_haveLocation) return false;

    char url[640];
    snprintf(url, sizeof(url),
        "http://api.open-meteo.com/v1/forecast"
        "?latitude=%.4f&longitude=%.4f"
        "&current=temperature_2m,relative_humidity_2m,apparent_temperature,"
                 "is_day,precipitation,weather_code,wind_speed_10m,"
                 "wind_direction_10m,uv_index"
        "&hourly=temperature_2m,weather_code,precipitation_probability"
        "&daily=weather_code,temperature_2m_max,temperature_2m_min,"
                "sunrise,sunset,precipitation_sum"
        "&timezone=auto&forecast_days=%d&past_days=0"
        "&temperature_unit=%s&wind_speed_unit=%s&precipitation_unit=%s"
        "&timeformat=unixtime",
        g_lat, g_lon, kDailyCount,
        WEATHER_TEMP_UNIT, WEATHER_WIND_UNIT, WEATHER_PRECIP_UNIT);

    Serial.printf("[wx] GET %s\n", url);

    HTTPClient http;
    http.setTimeout(12000);
    http.begin(url);
    int code = http.GET();
    if (code != 200) {
        Serial.printf("[wx] HTTP %d\n", code);
        http.end();
        return false;
    }

    // Read the full body into a String first.  Two reasons:
    //   1. Open-Meteo replies with Transfer-Encoding: chunked, and ArduinoJson
    //      v7's stream parser + Filter mode doesn't reliably consume chunked
    //      bodies — it can return "NoError" with an empty doc, leaving the
    //      raw chunk-size hex bytes in the TCP buffer.  Letting HTTPClient
    //      reassemble the body for us avoids that landmine entirely.
    //   2. With 8 MB of PSRAM we have plenty of headroom, so we don't need
    //      to filter to save RAM — just parse the whole document.
    String body = http.getString();
    http.end();
    Serial.printf("[wx] received %u bytes\n", body.length());
    if (body.length() == 0) {
        Serial.println("[wx] empty body");
        return false;
    }

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, body);
    if (err) {
        Serial.printf("[wx] json error: %s  body[0..120]=%s\n",
                      err.c_str(), body.substring(0, 120).c_str());
        return false;
    }

    if (!doc["current"].is<JsonObject>()) {
        Serial.println("[wx] missing \"current\" object");
        return false;
    }
    JsonObject c = doc["current"];

    if (!doc["hourly"].is<JsonObject>() || !doc["daily"].is<JsonObject>()) {
        Serial.println("[wx] missing hourly/daily objects");
        return false;
    }
    JsonArray times = doc["hourly"]["time"];
    JsonArray dTime = doc["daily"]["time"];
    if (times.size() == 0 || dTime.size() == 0) {
        Serial.printf("[wx] empty time series  hourly=%u daily=%u\n",
                      (unsigned)times.size(), (unsigned)dTime.size());
        return false;
    }

    // Parse into a fresh snapshot; assign g_weather only when complete so we
    // never leave a half-updated model on bad JSON.
    WeatherSnapshot snap;

    snap.current.valid       = true;
    snap.current.temp        = c["temperature_2m"]       | 0.0f;
    snap.current.feelsLike   = c["apparent_temperature"] | 0.0f;
    snap.current.humidity    = c["relative_humidity_2m"]  | 0.0f;
    snap.current.windSpeed   = c["wind_speed_10m"]       | 0.0f;
    snap.current.windDirDeg  = c["wind_direction_10m"]   | 0.0f;
    snap.current.precip      = c["precipitation"]         | 0.0f;
    snap.current.uvIndex     = c["uv_index"]              | 0.0f;
    snap.current.weatherCode = c["weather_code"]          | -1;
    snap.current.isDay       = (c["is_day"] | 1) != 0;

    // Hourly: next kHourlyCount slots from "now"
    {
        JsonArray temps = doc["hourly"]["temperature_2m"];
        JsonArray codes = doc["hourly"]["weather_code"];
        JsonArray pops  = doc["hourly"]["precipitation_probability"];
        time_t nowEpoch = time(nullptr);
        int startIdx    = 0;
        for (size_t i = 0; i < times.size(); ++i) {
            time_t t = times[i].as<long long>();
            if (t >= nowEpoch) {
                startIdx = (int)i;
                break;
            }
        }
        for (int i = 0; i < kHourlyCount; ++i) {
            int     j = startIdx + i;
            HourlyEntry e;
            if (j < (int)times.size()) {
                e.epoch       = (time_t)(times[j].as<long long>());
                e.temp        = temps[j] | 0.0f;
                e.weatherCode = codes[j] | -1;
                e.precipPct   = pops[j]  | 0;
            }
            snap.hourly[i] = e;
        }
    }

    // Daily
    {
        JsonArray dHi = doc["daily"]["temperature_2m_max"];
        JsonArray dLo = doc["daily"]["temperature_2m_min"];
        JsonArray dWc = doc["daily"]["weather_code"];
        JsonArray dSr = doc["daily"]["sunrise"];
        JsonArray dSs = doc["daily"]["sunset"];
        JsonArray dPs = doc["daily"]["precipitation_sum"];
        for (int i = 0; i < kDailyCount; ++i) {
            DailyEntry e;
            if ((size_t)i < dTime.size()) {
                e.epoch       = (time_t)(dTime[i].as<long long>());
                e.tempMax     = dHi[i] | 0.0f;
                e.tempMin     = dLo[i] | 0.0f;
                e.weatherCode = dWc[i] | -1;
                e.sunrise     = (time_t)(dSr[i].as<long long>());
                e.sunset      = (time_t)(dSs[i].as<long long>());
                e.precipSum   = dPs[i] | 0.0f;
            }
            snap.daily[i] = e;
        }
    }

    snap.ok        = true;
    snap.fetchedMs = millis();
    g_weather      = snap;

    Serial.printf("[wx] OK  now=%.0f%s feels=%.0f code=%d hourly=%d daily=%d\n",
                  g_weather.current.temp,
                  String(WEATHER_TEMP_UNIT).startsWith("c") ? "C" : "F",
                  g_weather.current.feelsLike,
                  g_weather.current.weatherCode,
                  kHourlyCount, kDailyCount);
    return true;
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
    drawText(&FiraSans_12, 28, y, title);
    drawHLine(28, y + 8, epd_rotated_display_width() - 56);
}

const char *tempUnitGlyph() {
    return String(WEATHER_TEMP_UNIT).startsWith("c") ? "C" : "F";
}

// ---------------------------------------------------------------------------
// Render
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
        hlSyncFramebuffersToWhite(&g_hl);
    } else {
        epd_hl_set_all_white(&g_hl);
    }

    char buf[160];

    // ============================================================ HEADER
    drawText(&FiraSans_20, 28, 50,
             g_haveLocation ? g_locationName.c_str() : "Locating...");

    struct tm tm;
    bool haveTime = WiFi.status() == WL_CONNECTED && getLocalTime(&tm);
    if (haveTime) {
        strftime(buf, sizeof(buf), "%a %b %d", &tm);
        drawText(&FiraSans_12, 28, 90, buf);
        strftime(buf, sizeof(buf), "%H:%M", &tm);
        drawText(&FiraSans_20, W - 130, 90, buf);
    } else {
        drawText(&FiraSans_12, 28, 90, "(no time)");
    }
    drawHLine(28, 110, W - 56);

    // ============================================================ NOW
    if (g_weather.ok && g_weather.current.valid) {
        const auto &c = g_weather.current;
        snprintf(buf, sizeof(buf), "%.0f°%s", c.temp, tempUnitGlyph());
        drawCenteredText(&FiraSans_20, 230, W / 2, buf);

        drawCenteredText(&FiraSans_20, 295, W / 2, wxLabel(c.weatherCode));

        snprintf(buf, sizeof(buf), "Feels %.0f°%s    Humid %.0f%%",
                 c.feelsLike, tempUnitGlyph(), c.humidity);
        drawCenteredText(&FiraSans_12, 340, W / 2, buf);

        snprintf(buf, sizeof(buf), "Wind %.0f %s %s    UV %.1f",
                 c.windSpeed, WEATHER_WIND_UNIT,
                 windCardinal(c.windDirDeg), c.uvIndex);
        drawCenteredText(&FiraSans_12, 370, W / 2, buf);

        snprintf(buf, sizeof(buf), "Precip last hr: %.2f %s",
                 c.precip, WEATHER_PRECIP_UNIT);
        drawCenteredText(&FiraSans_12, 400, W / 2, buf);
    } else {
        drawCenteredText(&FiraSans_20, 250, W / 2,
                         g_haveLocation ? "Fetching weather..."
                                        : "Locating...");
    }

    drawHLine(28, 430, W - 56);

    // ============================================================ HOURLY
    drawSectionHeader(465, "NEXT 6 HOURS");
    {
        const int colW = (W - 56) / kHourlyCount;
        for (int i = 0; i < kHourlyCount; ++i) {
            const auto &h = g_weather.hourly[i];
            int xCenter = 28 + colW / 2 + i * colW;

            if (h.epoch == 0) continue;
            struct tm tmh;
            localtime_r(&h.epoch, &tmh);
            snprintf(buf, sizeof(buf), "%02d:00", tmh.tm_hour);
            drawCenteredText(&FiraSans_12, 510, xCenter, buf);

            snprintf(buf, sizeof(buf), "%.0f°", h.temp);
            drawCenteredText(&FiraSans_20, 550, xCenter, buf);

            drawCenteredText(&FiraSans_12, 580, xCenter,
                             wxGlyph(h.weatherCode));

            snprintf(buf, sizeof(buf), "%d%%", h.precipPct);
            drawCenteredText(&FiraSans_12, 605, xCenter, buf);
        }
    }
    drawHLine(28, 625, W - 56);

    // ============================================================ DAILY
    drawSectionHeader(660, "NEXT 5 DAYS");
    {
        const int colW = (W - 56) / kDailyCount;
        for (int i = 0; i < kDailyCount; ++i) {
            const auto &d = g_weather.daily[i];
            int xCenter = 28 + colW / 2 + i * colW;
            if (d.epoch == 0) continue;
            struct tm tmd;
            localtime_r(&d.epoch, &tmd);
            strftime(buf, sizeof(buf), "%a", &tmd);
            drawCenteredText(&FiraSans_12, 705, xCenter, buf);

            snprintf(buf, sizeof(buf), "%.0f°", d.tempMax);
            drawCenteredText(&FiraSans_20, 745, xCenter, buf);

            snprintf(buf, sizeof(buf), "%.0f°", d.tempMin);
            drawCenteredText(&FiraSans_12, 775, xCenter, buf);

            drawCenteredText(&FiraSans_12, 800, xCenter,
                             wxGlyph(d.weatherCode));
        }
    }
    drawHLine(28, 825, W - 56);

    // ============================================================ FOOTER
    if (g_weather.ok && g_weather.daily[0].sunrise) {
        struct tm tmsr, tmss;
        localtime_r(&g_weather.daily[0].sunrise, &tmsr);
        localtime_r(&g_weather.daily[0].sunset,  &tmss);
        snprintf(buf, sizeof(buf),
                 "Sunrise %02d:%02d    Sunset %02d:%02d",
                 tmsr.tm_hour, tmsr.tm_min, tmss.tm_hour, tmss.tm_min);
        drawCenteredText(&FiraSans_12, 870, W / 2, buf);
    }

    uint16_t batMv = 0, batSoc = 0;
    bool     batChg = false;
    bool     batOk  = false;
    if (g_bqOk) {
        batOk = bq27220_idf_read_live(kBattI2cPort, &batMv, &batSoc, &batChg);
        if (batOk) {
            snprintf(buf, sizeof(buf), "%.2f V  %u%%  %s",
                     batMv / 1000.0f, (unsigned)batSoc,
                     batChg ? "charging" : "discharging");
        } else {
            snprintf(buf, sizeof(buf), "Battery: read error");
        }
    } else {
        snprintf(buf, sizeof(buf), "Battery: n/a (no BQ27220)");
    }
    drawCenteredText(&FiraSans_12, 895, W / 2, buf);

    const char *wifiTag =
        WiFi.status() == WL_CONNECTED ? "wifi:OK"
        : g_wasConnected               ? "wifi:RECONNECTING"
                                       : "wifi:DOWN";
    uint32_t ageMs = g_weather.ok ? (millis() - g_weather.fetchedMs) : 0;

    char nextBuf[32];
    formatNextWxAttempt(nextBuf, sizeof(nextBuf));

    if (g_weather.ok) {
        snprintf(buf, sizeof(buf),
                 "%s   ntp:%s   wx age %lus   next %s   #%lu",
                 wifiTag, g_haveNtpSynced ? "OK" : "PEND",
                 (unsigned long)(ageMs / 1000),
                 nextBuf,
                 (unsigned long)g_renderCount);
    } else {
        snprintf(buf, sizeof(buf),
                 "%s   ntp:%s   wx:NO DATA   retry %s   #%lu",
                 wifiTag, g_haveNtpSynced ? "OK" : "PEND",
                 nextBuf,
                 (unsigned long)g_renderCount);
    }
    drawCenteredText(&FiraSans_12, 920, W / 2, buf);

    // ============================================================ PUSH
    epd_poweron();
    epd_hl_update_screen(&g_hl, MODE_GC16, temp);
    epd_poweroff();

    char batLog[8];
    if (batOk) {
        snprintf(batLog, sizeof(batLog), "%u%%", (unsigned)batSoc);
    } else {
        snprintf(batLog, sizeof(batLog), "%s", g_bqOk ? "err" : "n/a");
    }
    Serial.printf(
        "[render #%lu] wx=%d ageMs=%lu wifi=%d ntp=%d bat=%s heap=%u\n",
        (unsigned long)g_renderCount, g_weather.ok, (unsigned long)ageMs,
        WiFi.status() == WL_CONNECTED, g_haveNtpSynced, batLog,
        ESP.getFreeHeap() / 1024U);
}

}  // namespace

void setup() {
    Serial.begin(115200);
    for (uint32_t start = millis(); !Serial && millis() - start < 3000;) {
        delay(50);
    }
    Serial.println();
    Serial.println("=== T5 E-Paper S3 Pro - Weather Dashboard ===");

    // Vendor factory brings up BQ27220 *before* epdiy: epd_board_v7 installs
    // ESP-IDF I2C on the same bus/pins; init'ing the gauge after that breaks
    // Arduino Wire and you'll get "Battery: n/a".  Order: Wire → fuel gauge → EPD.
    Wire.begin(kI2cSda, kI2cScl);
    initBattery();
    initEpd();
    initWifi();
    trySyncNtp(10000);

    // First fetch attempt at boot.  Subsequent attempts are driven by the
    // schedule below.
    if (WiFi.status() == WL_CONNECTED) {
        geolocateByIp();
        if (g_haveLocation) {
            g_lastFetchOk = fetchWeather();
            scheduleNextWxAttempt(g_lastFetchOk);
        } else {
            scheduleNextWxAttempt(false);
        }
    } else {
        scheduleNextWxAttempt(false);
    }

    renderDashboard();
    g_lastScreenMs = millis();
}

void loop() {
    maintainWifi();

    // ----- Weather fetch driven by schedule -----------------------------
    // Success -> next attempt at the top of the next hour, AND redraw now
    //            so the new data hits the panel immediately.
    // Failure -> retry in 60 s, NO immediate redraw — the failure state
    //            (stale "wx age", updated retry countdown) will be picked
    //            up by the next minute-tick redraw below, which keeps the
    //            panel from flashing every minute on a network outage.
    if (isWxFetchDue()) {
        bool ok = false;
        if (WiFi.status() == WL_CONNECTED) {
            if (!g_haveLocation) geolocateByIp();
            if (g_haveLocation) {
                ok = fetchWeather();
            } else {
                Serial.println("[wx] skip: no location yet");
            }
        } else {
            Serial.println("[wx] skip: WiFi down");
        }
        g_lastFetchOk = ok;
        scheduleNextWxAttempt(ok);

        if (ok) {
            renderDashboard();
            g_lastScreenMs = millis();
        }
    }

    // ----- Minute-tick redraw (clock + footer) --------------------------
    // Independent of weather state; runs every kScreenRefreshMs whether or
    // not the last fetch succeeded.
    if (millis() - g_lastScreenMs >= kScreenRefreshMs) {
        renderDashboard();
        g_lastScreenMs = millis();
    }

    delay(100);  // yield to FreeRTOS so IDLE can run
}
