#include "screens.h"

#include "epd_wrap.h"
#include "firasans_12.h"
#include "firasans_20.h"
#include "images.h"

#include <Arduino.h>
#include <LittleFS.h>
#include <esp_heap_caps.h>
#include <cstring>

namespace screens {

// ===========================================================================
// Shared layout constants
// ===========================================================================

// Back button visual indicator (top-left corner).  The tap zone is handled
// globally by ScreenManager::handleTouch — individual screens just draw the
// affordance.
static void drawBackButton() {
    epd::drawCenterText(&FiraSans_20, 40, 50, "<");
    epd::drawCenterText(&FiraSans_12, 40, 72, "back");
}

static void drawHeader(const char *title) {
    int W = epd::kWidth;
    epd::drawCenterText(&FiraSans_20, W / 2, 50, title);
    epd::drawHLine(20, 80, W - 40);
}

// ===========================================================================
// Lock Screen
// ===========================================================================

void LockScreen::onEnter() {
    int W = epd::kWidth;
    int H = epd::kHeight;

    epd::drawHLine(20, 120, W - 40);
    epd::drawHLine(20, H - 120, W - 40);

    epd::drawCenterText(&FiraSans_20, W / 2, 250, "MY PHONE");
    epd::drawCenterText(&FiraSans_12, W / 2, 500, "Tap anywhere to unlock");

    // Simple phone icon
    int cx = W / 2;
    int cy = 380;
    epd::fillRect(cx - 25, cy - 45, 50, 90, epd::kBlack);
    epd::fillRect(cx - 20, cy - 40, 40, 80, epd::kWhite);
    epd::fillRect(cx - 15, cy - 35, 30, 6, epd::kBlack);
    epd::fillRect(cx - 15, cy + 29, 30, 6, epd::kBlack);
}

bool LockScreen::onTouch(int, int) {
    return true;  // any tap unlocks
}

// ===========================================================================
// Launcher
// ===========================================================================

void LauncherScreen::getIcons(Icon *icons) const {
    int W = epd::kWidth;
    int startX = (W - (kCols * kIconSize + (kCols - 1) * kGap)) / 2;

    const char *labels[kRows * kCols] = {"Phone", "Gallery", "Clock", "Settings"};

    for (int i = 0; i < kRows * kCols; i++) {
        int col = i % kCols;
        int row = i / kCols;
        icons[i] = {
            labels[i],
            startX + col * (kIconSize + kGap),
            kMarginTop + row * (kIconSize + kGap + 40),
        };
    }
}

void LauncherScreen::onEnter() {
    int W = epd::kWidth;
    int H = epd::kHeight;

    drawHeader("Home");

    Icon icons[kRows * kCols];
    getIcons(icons);

    for (int i = 0; i < kRows * kCols; i++) {
        int x = icons[i].x;
        int y = icons[i].y;

        // Icon box: white fill, black border
        epd::fillRect(x, y, kIconSize, kIconSize, epd::kWhite);
        epd::drawHLine(x, y, kIconSize);
        epd::drawHLine(x, y + kIconSize - 1, kIconSize);
        epd::drawVLine(x, y, kIconSize);
        epd::drawVLine(x + kIconSize - 1, y, kIconSize);

        int cx = x + kIconSize / 2;
        int cy = y + kIconSize / 2 - 10;

        switch (i) {
            case 0: {  // Phone handset
                epd::fillRect(cx - 20, cy - 25, 40, 50, epd::kBlack);
                epd::fillRect(cx - 15, cy - 20, 30, 40, epd::kWhite);
                epd::fillRect(cx - 12, cy - 17, 24, 8, epd::kBlack);
                epd::fillRect(cx - 12, cy + 9, 24, 8, epd::kBlack);
                break;
            }
            case 1: {  // Gallery grid
                for (int gy = 0; gy < 2; gy++)
                    for (int gx = 0; gx < 2; gx++)
                        epd::fillRect(cx - 25 + gx * 24, cy - 20 + gy * 24,
                                      20, 20, epd::kBlack);
                break;
            }
            case 2: {  // Clock
                epd::fillRect(cx - 30, cy - 30, 60, 60, epd::kBlack);
                epd::fillRect(cx - 26, cy - 26, 52, 52, epd::kWhite);
                epd::drawVLine(cx, cy, 15, epd::kBlack);
                epd::drawHLine(cx, cy, 20, epd::kBlack);
                break;
            }
            case 3: {  // Settings gear
                epd::fillRect(cx - 25, cy - 25, 50, 50, epd::kBlack);
                epd::fillRect(cx - 20, cy - 20, 40, 40, epd::kWhite);
                epd::fillRect(cx - 8, cy - 8, 16, 16, epd::kBlack);
                break;
            }
        }

        epd::drawCenterText(&FiraSans_12, cx, y + kIconSize + 20, icons[i].label);
    }

    epd::drawCenterText(&FiraSans_12, W / 2, H - 30, "Tap an icon");
}

bool LauncherScreen::onTouch(int x, int y) {
    Icon icons[kRows * kCols];
    getIcons(icons);

    for (int i = 0; i < kRows * kCols; i++) {
        ui::TapZone zone = {icons[i].x, icons[i].y, kIconSize, kIconSize};
        if (zone.hit(x, y)) {
            Serial.printf("[launcher] tapped '%s'\n", icons[i].label);
            tappedIcon_ = i;
            return false;  // main.cpp handles the push
        }
    }
    return false;
}

// ===========================================================================
// Gallery: thumbnail grid (flat list — no categories)
// ===========================================================================

void GalleryGridScreen::onEnter() {
    int W = epd::kWidth;
    int count = images::kImageCount;

    drawBackButton();
    drawHeader("Photos");

    // 2-column x 3-row thumbnail grid.  Each cell is 240x240 + 20px padding.
    int cols = 2;
    int cellW = images::kThumbW + 20;   // 260
    int cellH = images::kThumbH + 20;   // 260
    int gridW = cols * cellW;           // 520
    int startX = (W - gridW) / 2;       // 10
    int startY = 110;

    for (int i = 0; i < count; i++) {
        int col = i % cols;
        int row = i / cols;
        int x = startX + col * cellW;
        int y = startY + row * cellH;

        // Load and draw the thumbnail
        char thumbPath[96];
        const char *fname = images::kImages[i];
        char thumbName[48];
        strncpy(thumbName, fname, sizeof(thumbName) - 1);
        thumbName[sizeof(thumbName) - 1] = '\0';
        char *dot = strchr(thumbName, '.');
        if (dot) *dot = '\0';
        snprintf(thumbPath, sizeof(thumbPath), "%s/thumb_%s.epd",
                 images::kDir, thumbName);

        File f = LittleFS.open(thumbPath, "r");
        if (f) {
            uint8_t *buf = (uint8_t *)heap_caps_malloc(images::kThumbSize, MALLOC_CAP_SPIRAM);
            if (buf) {
                size_t got = f.read(buf, images::kThumbSize);
                f.close();
                if (got == images::kThumbSize) {
                    // Thumbnail border (drawn in logical coords — epdiy rotates these)
                    epd::fillRect(x, y, images::kThumbW + 8, images::kThumbH + 8, epd::kWhite);
                    epd::drawHLine(x, y, images::kThumbW + 8);
                    epd::drawHLine(x, y + images::kThumbH + 7, images::kThumbW + 8);
                    epd::drawVLine(x, y, images::kThumbH + 8);
                    epd::drawVLine(x + images::kThumbW + 7, y, images::kThumbH + 8);

                    // Thumbnail is pre-rotated to physical 356x200 layout.
                    // EPD_ROT_INVERTED_PORTRAIT: phys = (ly, 540 - lx - lw).
                    int lx = x + 4;
                    int ly = y + 4;
                    int physX = ly;
                    int physY = 540 - lx - images::kThumbW;
                    EpdRect area = {.x = physX, .y = physY,
                                    .width = images::kThumbH, .height = images::kThumbW};
                    epd_copy_to_framebuffer(area, buf, epd::fb);
                }
                free(buf);
            } else {
                f.close();
            }
        } else {
            // Fallback: empty bordered cell
            epd::fillRect(x, y, images::kThumbW + 8, images::kThumbH + 8, epd::kWhite);
            epd::drawHLine(x, y, images::kThumbW + 8);
            epd::drawHLine(x, y + images::kThumbH + 7, images::kThumbW + 8);
            epd::drawVLine(x, y, images::kThumbH + 8);
            epd::drawVLine(x + images::kThumbW + 7, y, images::kThumbH + 8);
            epd::drawCenterText(&FiraSans_12, x + images::kThumbW / 2 + 4,
                                y + images::kThumbH / 2, "?");
        }
    }

    epd::drawCenterText(&FiraSans_12, W / 2, epd::kHeight - 30,
                        "Tap a picture  |  < back");
}

bool GalleryGridScreen::onTouch(int x, int y) {

    int cols = 2;
    int cellW = images::kThumbW + 20;
    int cellH = images::kThumbH + 20;
    int gridW = cols * cellW;
    int startX = (epd::kWidth - gridW) / 2;
    int startY = 110;

    for (int i = 0; i < images::kImageCount; i++) {
        int col = i % cols;
        int row = i / cols;
        int cx = startX + col * cellW;
        int cy = startY + row * cellH;
        ui::TapZone zone = {cx, cy, images::kThumbW + 8, images::kThumbH + 8};
        if (zone.hit(x, y)) {
            Serial.printf("[grid] tapped image %d\n", i);
            tappedImage_ = i;
            return false;  // main.cpp pushes the viewer
        }
    }
    return false;
}

// ===========================================================================
// Gallery: full-screen image viewer
// ===========================================================================

constexpr int kImageSize = 960 * 540 / 2;  // 259,200 bytes (4bpp, physical 960x540)
constexpr int kPhysW = 960;
constexpr int kPhysH = 540;

bool GalleryViewerScreen::loadImage(int index) {
    char path[80];
    snprintf(path, sizeof(path), "%s/%s", images::kDir, images::kImages[index]);

    Serial.printf("[viewer] loading %s ...\n", path);

    File f = LittleFS.open(path, "r");
    if (!f) {
        Serial.printf("[viewer] failed to open %s\n", path);
        return false;
    }

    uint8_t *buf = (uint8_t *)heap_caps_malloc(kImageSize, MALLOC_CAP_SPIRAM);
    if (!buf) {
        Serial.println("[viewer] PSRAM malloc failed");
        f.close();
        return false;
    }

    size_t got = f.read(buf, kImageSize);
    f.close();

    if (got != kImageSize) {
        Serial.printf("[viewer] read %u bytes, expected %u\n", got, kImageSize);
        free(buf);
        return false;
    }

    Serial.printf("[viewer] read OK, copying to framebuffer ...\n");

    epd::fillWhite();

    // Fast path: image is already in physical 960x540 layout.
    EpdRect area = {.x = 0, .y = 0, .width = kPhysW, .height = kPhysH};
    epd_copy_to_framebuffer(area, buf, epd::fb);

    Serial.printf("[viewer] copy done\n");
    free(buf);
    return true;
}

void GalleryViewerScreen::onEnter() {
    if (!loadImage(imageIndex_)) {
        epd::fillWhite();
        drawBackButton();
        epd::drawCenterText(&FiraSans_20, epd::kWidth / 2, 200, "No Image");
        epd::drawCenterText(&FiraSans_12, epd::kWidth / 2, 250,
                            "File not found in LittleFS");
        return;
    }

    int W = epd::kWidth;
    int H = epd::kHeight;

    // Semi-opaque white bars behind nav text so it's readable over the image.
    epd::fillRect(0, 0, 100, 60, 0xCC);
    drawBackButton();

    char counter[16];
    snprintf(counter, sizeof(counter), "%d / %d", imageIndex_ + 1, images::kImageCount);
    epd::fillRect(W / 2 - 50, 10, 100, 30, 0xCC);
    epd::drawCenterText(&FiraSans_12, W / 2, 30, counter);

    epd::fillRect(0, H - 50, W, 50, 0xCC);
    epd::drawCenterText(&FiraSans_12, W / 2, H - 25, "< prev  |  next >");
}

bool GalleryViewerScreen::onTouch(int x, int y) {
    int W = epd::kWidth;


    if (y > 80) {
        if (x < W / 2) {
            prev();
        } else {
            next();
        }
        return true;
    }
    return false;
}

void GalleryViewerScreen::next() {
    imageIndex_ = (imageIndex_ + 1) % images::kImageCount;
    // Don't call onEnter() here — ScreenManager::redraw() will do it.
}

void GalleryViewerScreen::prev() {
    imageIndex_ = (imageIndex_ - 1 + images::kImageCount) % images::kImageCount;
    // Don't call onEnter() here — ScreenManager::redraw() will do it.
}

// ===========================================================================
// Phone: dial pad + mock call screen
// ===========================================================================

// 3x4 dial pad: 1-9, *, 0, #
static constexpr char kDialKeys[4][3] = {
    {'1', '2', '3'},
    {'4', '5', '6'},
    {'7', '8', '9'},
    {'*', '0', '#'},
};

void PhoneScreen::pressKey(char c) {
    if (dialedLen_ < (int)sizeof(dialed_) - 1) {
        dialed_[dialedLen_++] = c;
        dialed_[dialedLen_] = '\0';
    }
    Serial.printf("[phone] dialed: %s\n", dialed_);
}

void PhoneScreen::deleteKey() {
    if (dialedLen_ > 0) {
        dialedLen_--;
        dialed_[dialedLen_] = '\0';
        Serial.printf("[phone] dialed: %s\n", dialed_);
    }
}

void PhoneScreen::startCall() {
    if (dialedLen_ == 0) return;
    calling_ = true;
    Serial.printf("[phone] calling %s...\n", dialed_);
}

void PhoneScreen::endCall() {
    calling_ = false;
    dialedLen_ = 0;
    dialed_[0] = '\0';
    Serial.println("[phone] call ended");
}

void PhoneScreen::onEnter() {
    int W = epd::kWidth;
    int H = epd::kHeight;

    if (calling_) {
        // --- Mock call screen ---
        epd::fillWhite();
        drawBackButton();  // not shown prominently, but still works

        epd::drawCenterText(&FiraSans_20, W / 2, 200, "Calling...");

        // Show dialed number in large text
        epd::drawCenterText(&FiraSans_20, W / 2, 350, dialed_);

        // Big red hang-up button (drawn as a black circle)
        int cx = W / 2;
        int cy = 650;
        epd::fillRect(cx - 60, cy - 60, 120, 120, epd::kBlack);
        epd::fillRect(cx - 50, cy - 50, 100, 100, epd::kWhite);
        epd::drawCenterText(&FiraSans_12, cx, cy + 5, "Hang Up");

        epd::drawCenterText(&FiraSans_12, W / 2, H - 30, "Tap to hang up");
        return;
    }

    // --- Dial pad screen ---
    epd::fillWhite();
    drawBackButton();
    drawHeader("Phone");

    // Dialed number display
    if (dialedLen_ > 0) {
        epd::drawCenterText(&FiraSans_20, W / 2, 130, dialed_);
    } else {
        epd::drawCenterText(&FiraSans_12, W / 2, 125, "Enter a number");
    }
    epd::drawHLine(40, 160, W - 80);

    // 3x4 key grid
    int cols = 3;
    int rows = 4;
    int keySize = 120;
    int gap = 15;
    int gridW = cols * keySize + (cols - 1) * gap;
    int startX = (W - gridW) / 2;
    int startY = 200;

    for (int r = 0; r < rows; r++) {
        for (int c = 0; c < cols; c++) {
            int x = startX + c * (keySize + gap);
            int y = startY + r * (keySize + gap);

            // Key box: white fill, black border
            epd::fillRect(x, y, keySize, keySize, epd::kWhite);
            epd::drawHLine(x, y, keySize);
            epd::drawHLine(x, y + keySize - 1, keySize);
            epd::drawVLine(x, y, keySize);
            epd::drawVLine(x + keySize - 1, y, keySize);

            // Key label
            char label[2] = {kDialKeys[r][c], '\0'};
            epd::drawCenterText(&FiraSans_20, x + keySize / 2,
                                y + keySize / 2 + 8, label);
        }
    }

    // Delete button (below the grid)
    int delY = startY + rows * (keySize + gap) + 5;
    epd::drawCenterText(&FiraSans_12, W / 2, delY, "Delete");

    // Green call button (drawn as a filled black rect with white text)
    int callY = delY + 30;
    int callW = 200;
    int callH = 50;
    int callX = (W - callW) / 2;
    epd::fillRect(callX, callY, callW, callH, epd::kBlack);
    epd::drawCenterText(&FiraSans_12, W / 2, callY + 30, "Call");
}

bool PhoneScreen::onTouch(int x, int y) {
    int W = epd::kWidth;

    if (calling_) {
        // Any tap on the call screen (except back) hangs up
        // Check hang-up button area (large zone in lower half)
        if (y > 400) {
            endCall();
            return true;  // redraw dial pad
        }
        return false;
    }


    // Dial pad keys
    int cols = 3;
    int rows = 4;
    int keySize = 120;
    int gap = 15;
    int gridW = cols * keySize + (cols - 1) * gap;
    int startX = (W - gridW) / 2;
    int startY = 200;

    for (int r = 0; r < rows; r++) {
        for (int c = 0; c < cols; c++) {
            int kx = startX + c * (keySize + gap);
            int ky = startY + r * (keySize + gap);
            ui::TapZone zone = {kx, ky, keySize, keySize};
            if (zone.hit(x, y)) {
                pressKey(kDialKeys[r][c]);
                return true;  // redraw
            }
        }
    }

    // Delete button
    int delY = startY + rows * (keySize + gap) + 5;
    ui::TapZone delZone = {W / 2 - 60, delY - 10, 120, 30};
    if (delZone.hit(x, y)) {
        deleteKey();
        return true;
    }

    // Call button
    int callY = delY + 30;
    int callW = 200;
    int callH = 50;
    int callX = (W - callW) / 2;
    ui::TapZone callZone = {callX, callY, callW, callH};
    if (callZone.hit(x, y)) {
        startCall();
        return true;
    }

    return false;
}

// ===========================================================================
// Clock: big digital time + date (NTP)
// ===========================================================================

void ClockScreen::drawClock() {
    int W = epd::kWidth;
    int H = epd::kHeight;

    epd::fillWhite();
    drawBackButton();
    drawHeader("Clock");

    time_t now = time(nullptr);
    if (now == 0) {
        epd::drawCenterText(&FiraSans_20, W / 2, H / 2, "No time");
        epd::drawCenterText(&FiraSans_12, W / 2, H / 2 + 30,
                            "WiFi/NTP not connected");
        return;
    }

    struct tm tm;
    localtime_r(&now, &tm);

    // Big time (HH:MM) — drawn using large text
    char timeBuf[8];
    strftime(timeBuf, sizeof(timeBuf), "%H:%M", &tm);
    epd::drawCenterText(&FiraSans_20, W / 2, 350, timeBuf);

    // Date below
    char dateBuf[32];
    strftime(dateBuf, sizeof(dateBuf), "%A", &tm);  // full weekday
    epd::drawCenterText(&FiraSans_12, W / 2, 420, dateBuf);

    char dateBuf2[32];
    strftime(dateBuf2, sizeof(dateBuf2), "%B %d, %Y", &tm);
    epd::drawCenterText(&FiraSans_12, W / 2, 450, dateBuf2);

    // AM/PM indicator
    char ampm[4];
    strftime(ampm, sizeof(ampm), "%p", &tm);
    epd::drawCenterText(&FiraSans_12, W / 2, 500, ampm);
}

void ClockScreen::onEnter() {
    drawClock();
}

bool ClockScreen::onTouch(int x, int y) {
    return false;
}

bool ClockScreen::onTick() {
    time_t now = time(nullptr);
    if (now == 0) return false;

    struct tm tm;
    localtime_r(&now, &tm);

    // Only redraw when the minute changes
    if (tm.tm_min != lastMinute_) {
        lastMinute_ = tm.tm_min;
        drawClock();
        return true;
    }
    return false;
}

// ===========================================================================
// Settings: visual-only toggle rows
// ===========================================================================

void SettingsScreen::onEnter() {
    int W = epd::kWidth;
    epd::fillWhite();
    drawBackButton();
    drawHeader("Settings");

    int rowH = 80;
    int rowW = W - 80;
    int rowX = 40;
    int startY = 120;
    int gap = 20;

    for (int i = 0; i < kRowCount; i++) {
        int y = startY + i * (rowH + gap);

        // Row background
        epd::fillRect(rowX, y, rowW, rowH, epd::kWhite);
        epd::drawHLine(rowX, y, rowW);
        epd::drawHLine(rowX, y + rowH - 1, rowW);
        epd::drawVLine(rowX, y, rowH);
        epd::drawVLine(rowX + rowW - 1, y, rowH);

        // Label
        epd::drawText(&FiraSans_12, rowX + 20, y + rowH / 2 + 4, labels_[i]);

        // Toggle switch on the right side
        int tw = 60;
        int th = 30;
        int tx = rowX + rowW - tw - 20;
        int ty = y + (rowH - th) / 2;

        if (toggles_[i]) {
            // ON: filled black with white knob on right
            epd::fillRect(tx, ty, tw, th, epd::kBlack);
            epd::fillRect(tx + tw - th + 2, ty + 2, th - 4, th - 4, epd::kWhite);
        } else {
            // OFF: white with border, knob on left
            epd::fillRect(tx, ty, tw, th, epd::kWhite);
            epd::drawHLine(tx, ty, tw);
            epd::drawHLine(tx, ty + th - 1, tw);
            epd::drawVLine(tx, ty, th);
            epd::drawVLine(tx + tw - 1, ty, th);
            epd::fillRect(tx + 2, ty + 2, th - 4, th - 4, epd::kBlack);
        }
    }

    epd::drawCenterText(&FiraSans_12, W / 2, epd::kHeight - 30,
                        "Tap a row to toggle  |  < back");
}

bool SettingsScreen::onTouch(int x, int y) {

    int rowH = 80;
    int rowW = epd::kWidth - 80;
    int rowX = 40;
    int startY = 120;
    int gap = 20;

    for (int i = 0; i < kRowCount; i++) {
        int ry = startY + i * (rowH + gap);
        ui::TapZone zone = {rowX, ry, rowW, rowH};
        if (zone.hit(x, y)) {
            toggles_[i] = !toggles_[i];
            Serial.printf("[settings] toggle %d (%s) -> %s\n", i,
                          labels_[i], toggles_[i] ? "ON" : "OFF");
            return true;
        }
    }
    return false;
}

}  // namespace screens
