// Toy Phone — e-paper phone UI for kids on the LilyGo T5 E-Paper S3 Pro.
// Portrait orientation (540x960, USB-C at bottom).

#include <Arduino.h>
#include <Wire.h>

#include "epd_wrap.h"
#include "touch.h"
#include "ui.h"
#include "screens.h"

namespace {

ui::ScreenManager g_mgr;

// Persistent screen instances
screens::LockScreen     s_lock;
screens::LauncherScreen s_launcher;
screens::PhoneScreen    s_phone;
screens::ClockScreen    s_clock;
screens::SettingsScreen s_settings;

// Gallery sub-screens are allocated on demand since they depend on the
// selected category/image index.  We keep a small pool and reassign them.
screens::GalleryScreen        s_gallery;
screens::GalleryGridScreen   *s_grid = nullptr;    // allocated on tap
screens::GalleryViewerScreen  *s_viewer = nullptr;  // allocated on tap

bool g_unlocked = false;

}  // namespace

void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println("\n=== Toy Phone ===");

    // Touch must init BEFORE epd — touch.begin() calls Wire.begin().
    touch::init();
    epd::init();

    g_mgr.push(&s_lock);
}

void loop() {
    int x, y;

    if (touch::pollTap(x, y)) {
        if (!g_unlocked) {
            g_unlocked = true;
            g_mgr.push(&s_launcher);
        } else {
            ui::Screen *cur = g_mgr.current();

        if (cur == &s_launcher) {
            s_launcher.tappedIcon_ = -1;
            g_mgr.handleTouch(x, y);
            if (s_launcher.tappedIcon_ >= 0) {
                switch (s_launcher.tappedIcon_) {
                    case 0: g_mgr.push(&s_phone);    break;
                    case 1: g_mgr.push(&s_gallery);  break;
                    case 2: g_mgr.push(&s_clock);    break;
                    case 3: g_mgr.push(&s_settings); break;
                }
            }
        } else if (cur == &s_gallery) {
            s_gallery.tappedCategory_ = -1;
            g_mgr.handleTouch(x, y);
            if (s_gallery.tappedCategory_ >= 0) {
                delete s_grid;
                s_grid = new screens::GalleryGridScreen(s_gallery.tappedCategory_);
                g_mgr.push(s_grid);
            }
        } else if (s_grid && cur == s_grid) {
            s_grid->tappedImage_ = -1;
            g_mgr.handleTouch(x, y);
            if (s_grid->tappedImage_ >= 0) {
                delete s_viewer;
                s_viewer = new screens::GalleryViewerScreen(
                    s_grid->category_, s_grid->tappedImage_);
                g_mgr.push(s_viewer);
            }
        } else {
            g_mgr.handleTouch(x, y);
        }
        }  // else (unlocked)
    }

    // Handle hardware home button
    if (touch::isPressed() && g_unlocked) {
        int hx, hy;
        touch::readPoint(hx, hy);
        if (touch::homeButtonPressed()) {
            g_mgr.handleHomeButton();
        }
    }

    g_mgr.tick();
    delay(50);
}
