// Toy Phone — e-paper phone UI for kids on the LilyGo T5 E-Paper S3 Pro.
// Portrait orientation (540x960, USB-C at bottom).

#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <LittleFS.h>
#include <time.h>

#include "epd_wrap.h"
#include "touch.h"
#include "ui.h"
#include "screens.h"
#include "secrets.h"

namespace {

ui::ScreenManager g_mgr;

// Persistent screen instances
screens::LockScreen     s_lock;
screens::LauncherScreen s_launcher;
screens::PhoneScreen    s_phone;
screens::ClockScreen    s_clock;
screens::SettingsScreen s_settings;

// Gallery sub-screens are allocated on demand.
screens::GalleryGridScreen    s_grid;               // persistent — no category needed
screens::GalleryViewerScreen *s_viewer = nullptr;   // allocated on tap

bool g_unlocked = false;

}  // namespace

void setup() {
    // Release GPIO holds from deep sleep / previous boot (factory example does this)
    gpio_hold_dis((gpio_num_t)9);   // GT911 RST
    gpio_hold_dis((gpio_num_t)3);   // GT911 IRQ
    gpio_deep_sleep_hold_dis();

    Serial.begin(115200);
    delay(500);
    Serial.println("\n=== Toy Phone ===");

    // Hardware reset the GT911 before the EPD takes over GPIO 9 (D8).
    // The GT911 needs a clean reset to enter scanning mode.
    pinMode(9, OUTPUT);
    digitalWrite(9, LOW);
    delay(10);
    digitalWrite(9, HIGH);
    delay(100);  // GT911 boot time after reset

    // Wire must init BEFORE epd — otherwise epdiy installs its own I2C
    // driver and breaks Wire.  The GT911 init comes AFTER epd so its I2C
    // state isn't disturbed by the EPD's driver install attempt.
    Wire.begin(39, 40);
    epd::init();
    touch::init();

    // LittleFS for gallery images
    if (!LittleFS.begin(true)) {
        Serial.println("[fs] LittleFS mount FAILED");
    } else {
        Serial.printf("[fs] LittleFS mounted, %u bytes total, %u used\n",
                      LittleFS.totalBytes(), LittleFS.usedBytes());
    }

    // WiFi for NTP clock (non-blocking — runs in background)
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    if (WiFi.status() == WL_CONNECTED) {
        configTzTime(TZ_INFO, "pool.ntp.org", "time.google.com");
    }

    g_mgr.push(&s_lock);
}

void loop() {
    int x, y;
    bool homePressed;

    // Heartbeat every 5s to confirm the device is alive
    static uint32_t lastHeartbeat = 0;
    if (millis() - lastHeartbeat > 5000) {
        lastHeartbeat = millis();
        Serial.printf("[hb] alive, depth=%d, screen='%s'\n",
                      g_mgr.depth(),
                      g_mgr.current() ? g_mgr.current()->name() : "?");
        Serial.flush();
    }

    if (touch::pollTap(x, y, homePressed)) {
        Serial.printf("[main] tap detected at (%d,%d), home=%d, unlocked=%d, depth=%d\n",
                      x, y, homePressed, g_unlocked, g_mgr.depth());
        Serial.flush();
        // Home button takes priority — go to launcher from anywhere
        if (homePressed && g_unlocked) {
            g_mgr.handleHomeButton();
        } else if (!g_unlocked) {
            g_unlocked = true;
            g_mgr.push(&s_launcher);
        } else if (g_mgr.current()) {
            ui::Screen *cur = g_mgr.current();

            if (cur == &s_launcher) {
                s_launcher.tappedIcon_ = -1;
                g_mgr.handleTouch(x, y);
                if (s_launcher.tappedIcon_ >= 0) {
                    switch (s_launcher.tappedIcon_) {
                        case 0: g_mgr.push(&s_phone);    break;
                        case 1: g_mgr.push(&s_grid);     break;
                        case 2: g_mgr.push(&s_clock);    break;
                        case 3: g_mgr.push(&s_settings); break;
                    }
                }
            } else if (cur == &s_grid) {
                s_grid.tappedImage_ = -1;
                g_mgr.handleTouch(x, y);
                if (s_grid.tappedImage_ >= 0) {
                    delete s_viewer;
                    s_viewer = new screens::GalleryViewerScreen(s_grid.tappedImage_);
                    if (s_viewer) {
                        g_mgr.push(s_viewer);
                    }
                }
            } else {
                g_mgr.handleTouch(x, y);
            }
        }
    }

    g_mgr.tick();

    // Periodic GT911 health check — the EPD's GPIO 9 (D8) activity can reset
    // or sleep the GT911.  Check if it's alive and re-init if it stops
    // detecting touches.
    static uint32_t lastHealthCheck = 0;
    if (millis() - lastHealthCheck > 3000) {
        lastHealthCheck = millis();
        if (!touch::healthCheck()) {
            Serial.println("[main] GT911 unhealthy, re-initializing");
            touch::reinit();
        }
    }

    // Maintain WiFi + NTP in background
    if (g_unlocked && WiFi.status() == WL_CONNECTED) {
        static bool ntpConfigured = false;
        if (!ntpConfigured) {
            configTzTime(TZ_INFO, "pool.ntp.org", "time.google.com");
            ntpConfigured = true;
        }
    }

    delay(50);
}
