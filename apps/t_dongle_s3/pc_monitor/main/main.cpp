// T-Dongle S3: PC system monitor (serial-pushed stats).
//
// Physical orientation: **USB-A on top, portrait**. ST7735 is natively
// 80x160 portrait; rotation 2 flips it so the display reads correctly
// with the USB plug pointing up.
//
// Protocol: host writes one line per update to the USB CDC serial port:
//   T=10:47 C=23 M=45 L=0.42 W=48 D=12.3 U=4.5
//   T  = HH:MM wall clock        C = CPU %
//   M  = RAM %                   L = loadavg 1m
//   W  = CPU temp degC           D/U = net down/up KB/s
// Firmware repaints only when a complete line arrives (host-paced, slow).

#include <Arduino.h>
#include <TFT_eSPI.h>

namespace {

constexpr uint8_t  kTftRotation  = 2;  // USB-A on top
constexpr uint8_t  kBacklightPin = 38; // active LOW

// Consider data stale after this long without an update line.
constexpr uint32_t kStaleMs = 20000;

TFT_eSPI tft;

struct Stats {
    // Wall clock runs locally: host pushes T=HH:MM (24h; seconds optional,
    // assumed :00), firmware ticks with millis() and re-calibrates on every
    // push. No RTC/NTP needed.
    long     baseSec  = -1;    // seconds since midnight at last T= push
    uint32_t baseMillis = 0;   // millis() when that push was parsed
    int      cpu     = -1;
    int      mem     = -1;
    float    load    = -1.0f;
    int      temp    = -1;
    float    downKbs = -1.0f;
    float    upKbs   = -1.0f;
    uint32_t updatedAt = 0;  // millis() of last complete line
};

Stats g_stats;
char  g_line[96];
size_t g_lineLen = 0;
bool  g_dirty = false;

uint16_t pctColor(int pct) {
    if (pct < 60) {
        return tft.color565(0x2E, 0xD5, 0x73);  // green
    }
    if (pct < 85) {
        return tft.color565(0xFF, 0xC8, 0x28);  // amber
    }
    return tft.color565(0xFF, 0x4D, 0x4D);      // red
}

void backlightOn() {
    pinMode(kBacklightPin, OUTPUT);
    digitalWrite(kBacklightPin, LOW);
}

void applyToken(const char *tok) {
    if (tok[0] == '\0' || tok[1] != '=') {
        return;
    }
    const char *v = tok + 2;
    switch (tok[0]) {
    case 'T': {
        int h = 0, m = 0, s = 0;
        if (sscanf(v, "%d:%d:%d", &h, &m, &s) >= 2) {
            g_stats.baseSec = (long)h * 3600 + (long)m * 60 + s;
            g_stats.baseMillis = millis();
        }
        break;
    }
    case 'C':
        g_stats.cpu = atoi(v);
        break;
    case 'M':
        g_stats.mem = atoi(v);
        break;
    case 'L':
        g_stats.load = atof(v);
        break;
    case 'W':
        g_stats.temp = atoi(v);
        break;
    case 'D':
        g_stats.downKbs = atof(v);
        break;
    case 'U':
        g_stats.upKbs = atof(v);
        break;
    default:
        break;
    }
}

void parseLine(char *line) {
    char *save = nullptr;
    for (char *tok = strtok_r(line, " ", &save); tok; tok = strtok_r(nullptr, " ", &save)) {
        applyToken(tok);
    }
    g_stats.updatedAt = millis();
    g_dirty = true;
}

void pollSerial() {
    while (Serial.available() > 0) {
        char c = (char)Serial.read();
        if (c == '\n') {
            g_line[g_lineLen] = '\0';
            if (g_lineLen > 0) {
                parseLine(g_line);
            }
            g_lineLen = 0;
        } else if (c != '\r') {
            if (g_lineLen < sizeof(g_line) - 1) {
                g_line[g_lineLen++] = c;
            } else {
                g_lineLen = 0;  // overflow: drop line
            }
        }
    }
}

void formatKbs(char *out, size_t len, float kbs) {
    if (kbs >= 1024.0f) {
        snprintf(out, len, "%.1fM", kbs / 1024.0f);
    } else {
        snprintf(out, len, "%.0fK", kbs);
    }
}

void drawBar(int y, int pct, uint16_t color) {
    constexpr int x = 2;
    constexpr int w = 76;
    constexpr int h = 7;
    int fill = (pct < 0) ? 0 : (w * (pct > 100 ? 100 : pct)) / 100;
    tft.drawRect(x, y, w, h, TFT_DARKGREY);
    tft.fillRect(x + 1, y + 1, w - 2, h - 2, TFT_BLACK);
    if (fill > 2) {
        tft.fillRect(x + 1, y + 1, fill - 2, h - 2, color);
    }
}

// Current wall-clock minute (locally ticked), or -1 if no T= received yet.
int currentMinute() {
    if (g_stats.baseSec < 0) {
        return -1;
    }
    long nowSec = g_stats.baseSec + (long)((millis() - g_stats.baseMillis) / 1000);
    nowSec %= 86400;
    if (nowSec < 0) {
        nowSec += 86400;
    }
    return (int)(nowSec / 60);
}

void paint() {
    tft.fillScreen(TFT_BLACK);
    const int cx = tft.width() / 2;
    bool stale = (millis() - g_stats.updatedAt) > kStaleMs;
    uint16_t dim = stale ? TFT_DARKGREY : TFT_WHITE;

    // Time
    char timeStr[8] = "--:--";
    int minute = currentMinute();
    if (minute >= 0) {
        snprintf(timeStr, sizeof(timeStr), "%02d:%02d", minute / 60, minute % 60);
    }
    tft.setTextDatum(MC_DATUM);
    tft.setTextFont(4);
    tft.setTextSize(1);
    tft.setTextColor(stale ? TFT_DARKGREY : TFT_CYAN, TFT_BLACK);
    tft.drawString(timeStr, cx, 15);

    // Load line
    char buf[40];
    tft.setTextFont(2);
    tft.setTextColor(dim, TFT_BLACK);
    if (g_stats.load >= 0.0f) {
        snprintf(buf, sizeof(buf), "load %.2f", g_stats.load);
    } else {
        snprintf(buf, sizeof(buf), "load --");
    }
    tft.drawString(buf, cx, 34);

    // CPU row
    tft.setTextDatum(ML_DATUM);
    if (g_stats.cpu >= 0) {
        snprintf(buf, sizeof(buf), "CPU %d%%", g_stats.cpu);
        tft.setTextColor(pctColor(g_stats.cpu), TFT_BLACK);
    } else {
        snprintf(buf, sizeof(buf), "CPU --");
        tft.setTextColor(dim, TFT_BLACK);
    }
    tft.drawString(buf, 2, 50);
    if (g_stats.cpu >= 0) {
        drawBar(58, g_stats.cpu, pctColor(g_stats.cpu));
    }

    // MEM row
    if (g_stats.mem >= 0) {
        snprintf(buf, sizeof(buf), "MEM %d%%", g_stats.mem);
        tft.setTextColor(pctColor(g_stats.mem), TFT_BLACK);
    } else {
        snprintf(buf, sizeof(buf), "MEM --");
        tft.setTextColor(dim, TFT_BLACK);
    }
    tft.drawString(buf, 2, 72);
    if (g_stats.mem >= 0) {
        drawBar(80, g_stats.mem, pctColor(g_stats.mem));
    }

    // Temp
    tft.setTextDatum(MC_DATUM);
    if (g_stats.temp >= 0) {
        snprintf(buf, sizeof(buf), "%d C", g_stats.temp);
        uint16_t c = g_stats.temp < 65 ? tft.color565(0x2E, 0xD5, 0x73)
                   : g_stats.temp < 80 ? tft.color565(0xFF, 0xC8, 0x28)
                                       : tft.color565(0xFF, 0x4D, 0x4D);
        tft.setTextColor(stale ? TFT_DARKGREY : c, TFT_BLACK);
    } else {
        snprintf(buf, sizeof(buf), "-- C");
        tft.setTextColor(dim, TFT_BLACK);
    }
    tft.drawString(buf, cx, 100);

    // Net
    if (g_stats.downKbs >= 0.0f) {
        char d[10], u[10];
        formatKbs(d, sizeof(d), g_stats.downKbs);
        formatKbs(u, sizeof(u), g_stats.upKbs);
        snprintf(buf, sizeof(buf), "v%s ^%s", d, u);
    } else {
        snprintf(buf, sizeof(buf), "net --");
    }
    tft.setTextColor(dim, TFT_BLACK);
    tft.drawString(buf, cx, 120);

    // Status line
    if (stale) {
        tft.setTextColor(tft.color565(0xFF, 0x4D, 0x4D), TFT_BLACK);
        tft.drawString("OFFLINE", cx, 145);
    } else {
        tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
        tft.drawString("r7", cx, 145);
    }
}

void paintWaiting() {
    tft.fillScreen(TFT_BLACK);
    tft.setTextFont(2);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(TFT_CYAN, TFT_BLACK);
    tft.drawString("PC monitor", tft.width() / 2, tft.height() / 2 - 10);
    tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
    tft.drawString("waiting...", tft.width() / 2, tft.height() / 2 + 10);
}

}  // namespace

void setup() {
    Serial.begin(115200);
    tft.init();
    tft.setRotation(kTftRotation);
    tft.fillScreen(TFT_BLACK);
    backlightOn();
    paintWaiting();
}

void loop() {
    pollSerial();

    if (g_stats.updatedAt == 0) {
        delay(100);
        return;  // nothing received yet
    }

    static bool s_wasStale = false;
    static int  s_lastMinute = -2;
    bool stale = (millis() - g_stats.updatedAt) > kStaleMs;
    int  minute = currentMinute();

    if (g_dirty || stale != s_wasStale || minute != s_lastMinute) {
        g_dirty = false;
        s_wasStale = stale;
        s_lastMinute = minute;
        paint();
    }

    delay(50);
}
