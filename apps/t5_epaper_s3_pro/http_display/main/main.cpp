// HTTP Display — bare skeleton for LilyGo T5 E-Paper S3 Pro (H752-02).
//
// Renders a 540x960 portrait screen.  Two modes:
//
//   POST /display   body: {"text": "Line 1\nLine 2\n..."}
//                     → raw text with word-wrap
//
//   POST /dashboard body: {"title":"...","subtitle":"...",
//                          "sections":[{"header":"...","rows":[["col1","col2"],"plain text"]}]}
//                     → structured dashboard with headers, columns, separators
//
//   GET  /status    returns {"ssid":"...","ip":"...","connected":true,"uptime_s":123}

#include <Arduino.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Wire.h>
#include <time.h>
#include <cstring>

#include <epdiy.h>
#include "bq27220.h"

#include "firasans_12.h"
#include "firasans_20.h"
#include "secrets.h"

namespace {

// ---------------------------------------------------------------------------
// Pins + EPD constants
// ---------------------------------------------------------------------------
constexpr uint8_t kI2cSda         = 39;
constexpr uint8_t kI2cScl         = 40;
constexpr int     kVcomMillivolts = 1560;
constexpr int     kEpdRotation    = EPD_ROT_INVERTED_LANDSCAPE;  // 960x540, USB-C on left

constexpr uint8_t kColorWhite = 0xFF;
constexpr uint8_t kColorBlack = 0x00;

constexpr int kMarginX      = 20;   // left/right margin
constexpr int kUsableWidth  = 540 - (kMarginX * 2);  // 500 px

// ---------------------------------------------------------------------------
// Globals
// ---------------------------------------------------------------------------
EpdiyHighlevelState g_hl;
uint8_t            *g_fb = nullptr;
BQ27220             g_bq;
bool                g_bqOk = false;

WebServer           g_server(80);

enum class Mode { TEXT, DASHBOARD };
static Mode   g_mode  = Mode::TEXT;
static char   g_payload[4096] = "";
static bool   g_dirty = true;

// ---------------------------------------------------------------------------
// EPD + battery init
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
    g_bqOk = g_bq.init();
    Serial.printf("[bat] BQ27220 %s\n", g_bqOk ? "ok" : "FAILED");
}

// ---------------------------------------------------------------------------
// WiFi
// ---------------------------------------------------------------------------
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
                      WiFi.localIP().toString().c_str(), WiFi.RSSI());
        configTzTime("PST8PDT,M3.2.0,M11.1.0", "pool.ntp.org", "time.google.com");
    } else {
        Serial.println("[wifi] not yet connected — will retry in background");
    }
}

void maintainWifi() {
    if (WiFi.status() != WL_CONNECTED) {
        static uint32_t lastTry = 0;
        if (millis() - lastTry > 5000) {
            lastTry = millis();
            Serial.println("[wifi] reconnect...");
            WiFi.reconnect();
        }
    }
}

// ---------------------------------------------------------------------------
// Drawing primitives
// ---------------------------------------------------------------------------
void drawText(const EpdFont *font, int x, int y, const char *s,
              EpdFontFlags flags = (EpdFontFlags)0) {
    int cx = x;
    int cy = y;
    EpdFontProperties props = epd_font_properties_default();
    props.flags = flags;
    epd_write_string(font, s, &cx, &cy, g_fb, &props);
}

void drawRightText(const EpdFont *font, int xRight, int y, const char *s) {
    drawText(font, xRight, y, s, EPD_DRAW_ALIGN_RIGHT);
}

void drawCenterText(const EpdFont *font, int xCenter, int y, const char *s) {
    drawText(font, xCenter, y, s, EPD_DRAW_ALIGN_CENTER);
}

void drawHLine(int x, int y, int w, uint8_t color = kColorBlack) {
    EpdRect r = {.x = x, .y = y, .width = w, .height = 1};
    epd_fill_rect(r, color, g_fb);
}

// ---------------------------------------------------------------------------
// Word-wrap raw text
// ---------------------------------------------------------------------------
void renderText() {
    epd_hl_set_all_white(&g_hl);

    const int W = epd_rotated_display_width();
    drawText(&FiraSans_12, kMarginX, 22, "HTTP DISPLAY");
    drawHLine(kMarginX, 32, W - kMarginX * 2);

    int cy = 50;
    int lineCount = 0;
    const char *p = g_payload;
    static char buf[256];
    constexpr int kWrapChars = 70;   // conservative for FiraSans 12

    while (p && *p && lineCount < 30) {
        const char *eol = std::strchr(p, '\n');
        int remain = eol ? (int)(eol - p) : (int)std::strlen(p);

        const char *seg = p;
        while (remain > 0 && lineCount < 30) {
            int cut = (remain <= kWrapChars) ? remain : kWrapChars;
            if (cut == kWrapChars) {
                for (int i = cut - 1; i > 0 && i >= cut - 8; i--) {
                    if (seg[i] == ' ') { cut = i; break; }
                }
            }
            if (cut > remain) cut = remain;
            if (cut >= 255) cut = 254;

            std::memcpy(buf, seg, cut);
            buf[cut] = '\0';
            drawText(&FiraSans_12, kMarginX, cy, buf);

            seg += cut;
            remain -= cut;
            if (*seg == ' ' && remain > 0) { seg++; remain--; }
            cy += 28;
            lineCount++;
        }
        p = eol ? eol + 1 : nullptr;
    }

    char footer[64];
    int footerY = epd_rotated_display_height() - 16;
    std::snprintf(footer, sizeof(footer),
                  "%d lines  heap %u KB  %s",
                  lineCount, ESP.getFreeHeap() / 1024U,
                  WiFi.status() == WL_CONNECTED ? "wifi:OK" : "wifi:OFF");
    drawText(&FiraSans_12, kMarginX, footerY, footer);
}

// ---------------------------------------------------------------------------
// Format local Pacific Time
// ---------------------------------------------------------------------------
void formatLocalTime(char *out, size_t outLen) {
    time_t now = time(nullptr);
    if (now == 0) {
        snprintf(out, outLen, "--:--");
        return;
    }
    struct tm tm;
    localtime_r(&now, &tm);
    strftime(out, outLen, "%a %b %d, %Y %H:%M", &tm);
}

// ---------------------------------------------------------------------------
// Structured dashboard
// ---------------------------------------------------------------------------
void renderDashboard() {
    epd_hl_set_all_white(&g_hl);

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, g_payload);
    if (err) {
        drawText(&FiraSans_12, kMarginX, 50, "[dashboard] bad JSON");
        return;
    }

    const int W = epd_rotated_display_width();
    int cy = 42;

    // Title + date/time
    drawText(&FiraSans_20, kMarginX, cy, "Agent Status");
    char timeBuf[32];
    formatLocalTime(timeBuf, sizeof(timeBuf));
    drawRightText(&FiraSans_12, W - kMarginX, cy + 4, timeBuf);

    cy += 44;

    // Sections
    JsonArray sections = doc["sections"];
    for (JsonObject sec : sections) {
        const char *header = sec["header"] | "";
        if (header[0]) {
            drawText(&FiraSans_12, kMarginX, cy, header);
            cy += 28;
        }

        JsonArray rows = sec["rows"];
        for (JsonVariant row : rows) {
            if (cy > epd_rotated_display_height() - 40) break;

            if (row.is<const char*>()) {
                // Single string row — skip empty lines
                const char *text = row.as<const char*>();
                if (!text || text[0] == '\0') {
                    cy += 6;  // tiny gap for blank lines
                    continue;
                }
                drawText(&FiraSans_12, kMarginX, cy, text);
                cy += 30;
            } else if (row.is<JsonArray>()) {
                // Multi-column row
                JsonArray cols = row.as<JsonArray>();
                int n = cols.size();
                if (n == 2) {
                    // label | value  (value right-aligned)
                    const char *left  = cols[0].as<const char*>();
                    const char *right = cols[1].as<const char*>();
                    drawText(&FiraSans_12, kMarginX, cy, left ? left : "");
                    drawRightText(&FiraSans_12, W - kMarginX, cy, right ? right : "");
                    cy += 30;
                } else if (n == 3) {
                    // name | status | time
                    const char *a = cols[0].as<const char*>();
                    const char *b = cols[1].as<const char*>();
                    const char *c = cols[2].as<const char*>();
                    drawText(&FiraSans_12, kMarginX,        cy, a ? a : "");
                    drawCenterText(&FiraSans_12, W / 2,       cy, b ? b : "");
                    drawRightText(&FiraSans_12, W - kMarginX, cy, c ? c : "");
                    cy += 30;
                } else {
                    // Fallback: just join with spaces
                    static char line[128];
                    line[0] = '\0';
                    for (int i = 0; i < n && i < 4; i++) {
                        const char *v = cols[i].as<const char*>();
                        if (!v) v = "";
                        if (i > 0) std::strncat(line, "  ", sizeof(line) - std::strlen(line) - 1);
                        std::strncat(line, v, sizeof(line) - std::strlen(line) - 1);
                    }
                    drawText(&FiraSans_12, kMarginX, cy, line);
                    cy += 30;
                }
            }
        }
        cy += 18;  // gap between sections
    }

    // Footer
    char footer[80];
    int footerY = epd_rotated_display_height() - 20;
    std::snprintf(footer, sizeof(footer),
                  "heap %u KB  %s  uptime %lus",
                  ESP.getFreeHeap() / 1024U,
                  WiFi.status() == WL_CONNECTED ? "wifi:OK" : "wifi:OFF",
                  (unsigned long)(millis() / 1000));
    drawCenterText(&FiraSans_12, W / 2, footerY, footer);
}

// ---------------------------------------------------------------------------
// Push to panel
// ---------------------------------------------------------------------------
void renderScreen() {
    if (!g_dirty) return;
    g_dirty = false;

    if (g_mode == Mode::DASHBOARD) {
        renderDashboard();
    } else {
        renderText();
    }

    epd_poweron();
    epd_hl_update_screen(&g_hl, MODE_GC16, epd_ambient_temperature());
    epd_poweroff();

    Serial.println("[render] done");
}

// ---------------------------------------------------------------------------
// HTTP handlers
// ---------------------------------------------------------------------------
void handleDisplay() {
    if (g_server.method() != HTTP_POST) {
        g_server.send(405, "text/plain", "POST only");
        return;
    }
    String body = g_server.arg("plain");
    if (body.length() == 0) {
        g_server.send(400, "text/plain", "empty body");
        return;
    }
    JsonDocument doc;
    if (deserializeJson(doc, body)) {
        g_server.send(400, "text/plain", "bad JSON");
        return;
    }
    const char *text = doc["text"] | "";
    std::strncpy(g_payload, text, sizeof(g_payload) - 1);
    g_payload[sizeof(g_payload) - 1] = '\0';
    g_mode  = Mode::TEXT;
    g_dirty = true;
    g_server.send(200, "text/plain", "ok");
}

void handleDashboard() {
    if (g_server.method() != HTTP_POST) {
        g_server.send(405, "text/plain", "POST only");
        return;
    }
    String body = g_server.arg("plain");
    if (body.length() == 0) {
        g_server.send(400, "text/plain", "empty body");
        return;
    }
    if (body.length() >= sizeof(g_payload)) {
        g_server.send(413, "text/plain", "payload too large");
        return;
    }
    body.toCharArray(g_payload, sizeof(g_payload));
    g_mode  = Mode::DASHBOARD;
    g_dirty = true;
    g_server.send(200, "text/plain", "ok");
}

void handleStatus() {
    JsonDocument doc;
    doc["connected"] = (WiFi.status() == WL_CONNECTED);
    doc["ssid"]      = WiFi.SSID().c_str();
    doc["ip"]        = WiFi.localIP().toString().c_str();
    doc["rssi"]      = WiFi.RSSI();
    doc["uptime_s"]  = millis() / 1000;
    doc["heap_kb"]   = ESP.getFreeHeap() / 1024U;
    doc["mode"]      = (g_mode == Mode::DASHBOARD) ? "dashboard" : "text";
    doc["dirty"]     = g_dirty;

    static char buf[256];
    serializeJson(doc, buf, sizeof(buf));
    g_server.send(200, "application/json", buf);
}

void handleRoot() {
    g_server.send(200, "text/html", R"rawliteral(
<!doctype html>
<meta charset="utf-8">
<title>E-Paper Display</title>
<style>
body{font-family:sans-serif;max-width:600px;margin:2em auto}
form{margin:1em 0}
textarea{width:100%;height:120px}
</style>
<h2>Raw text</h2>
<form method="POST" action="/display">
<textarea name="text" placeholder="Plain text...">{"text": "Hello E-Paper"}</textarea>
<br><button>Send text</button>
</form>

<h2>Dashboard</h2>
<form method="POST" action="/dashboard" id="dashForm">
<textarea id="dashJson">{
  "title": "AGENTS",
  "subtitle": "14:23",
  "sections": [
    {"header": "STATUS",
     "rows": [["pi-coding", "● idle", "2m ago"],
              ["claude", "● thinking", "47s ago"]]},
    {"header": "QUEUE",
     "rows": ["3 pending", "• Refactor auth", "• Write tests"]},
    {"header": "TODAY",
     "rows": [["tokens", "1.2M"], ["cost", "$2.41"]]}
  ]
}</textarea>
<br><button>Send dashboard</button>
</form>
<pre id="status"></pre>
<script>
document.getElementById('dashForm').onsubmit=function(e){
  e.preventDefault();
  fetch('/dashboard',{method:'POST',headers:{'Content-Type':'application/json'},
    body:document.getElementById('dashJson').value})
  .then(r=>r.text()).then(t=>console.log(t));
};
fetch('/status').then(r=>r.text()).then(t=>document.getElementById('status').textContent=t);
</script>
)rawliteral");
}

}  // namespace

void setup() {
    Serial.begin(115200);
    for (uint32_t start = millis(); !Serial && millis() - start < 3000;) {
        delay(50);
    }
    Serial.println();
    Serial.println("=== HTTP Display (bare skeleton) ===");

    Wire.begin(kI2cSda, kI2cScl);
    initBattery();
    initEpd();
    initWifi();

    std::strncpy(g_payload,
        "HTTP DISPLAY\n\n"
        "Ready. Waiting for input.\n\n"
        "Usage:\n"
        "  POST /display   {\"text\": \"...\"}\n"
        "  POST /dashboard {\"title\": \"...\", \"sections\": [...]}\n"
        "  GET  /status\n\n"
        "Display: 540x960 (portrait)\n",
        sizeof(g_payload));
    g_payload[sizeof(g_payload) - 1] = '\0';

    g_server.on("/", handleRoot);
    g_server.on("/display", handleDisplay);
    g_server.on("/dashboard", handleDashboard);
    g_server.on("/status", handleStatus);
    g_server.begin();

    IPAddress ip = WiFi.localIP();
    Serial.printf("[http] server on http://%u.%u.%u.%u\n",
                  ip[0], ip[1], ip[2], ip[3]);

    renderScreen();
}

void loop() {
    maintainWifi();
    g_server.handleClient();
    renderScreen();
    delay(20);
}
