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

// Back button: top-left 80x80 tap zone on every screen (except lock/launcher).
static constexpr ui::TapZone kBackZone = {0, 0, 80, 80};

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
// Gallery: category list
// ===========================================================================

void GalleryScreen::onEnter() {
    int W = epd::kWidth;
    drawBackButton();
    drawHeader("Gallery");

    // Two large category buttons
    int btnW = W - 80;
    int btnH = 120;
    int btnX = 40;

    for (int i = 0; i < images::kCategoryCount; i++) {
        int y = 120 + i * (btnH + 30);

        // Button background (white) with black border
        epd::fillRect(btnX, y, btnW, btnH, epd::kWhite);
        epd::drawHLine(btnX, y, btnW);
        epd::drawHLine(btnX, y + btnH - 1, btnW);
        epd::drawVLine(btnX, y, btnH);
        epd::drawVLine(btnX + btnW - 1, y, btnH);

        // Category name + image count
        epd::drawCenterText(&FiraSans_20, W / 2, y + 45, images::kCategories[i].name);
        char count[32];
        snprintf(count, sizeof(count), "%d pictures",
                 images::kCategories[i].count);
        epd::drawCenterText(&FiraSans_12, W / 2, y + 80, count);
    }

    epd::drawCenterText(&FiraSans_12, W / 2, epd::kHeight - 30, "Tap a category");
}

bool GalleryScreen::onTouch(int x, int y) {
    if (kBackZone.hit(x, y)) return true;  // pop

    int btnW = epd::kWidth - 80;
    int btnH = 120;
    int btnX = 40;

    for (int i = 0; i < images::kCategoryCount; i++) {
        int by = 120 + i * (btnH + 30);
        ui::TapZone zone = {btnX, by, btnW, btnH};
        if (zone.hit(x, y)) {
            Serial.printf("[gallery] tapped category %d\n", i);
            tappedCategory_ = i;
            return false;  // main.cpp pushes the grid
        }
    }
    return false;
}

// ===========================================================================
// Gallery: thumbnail grid
// ===========================================================================

void GalleryGridScreen::onEnter() {
    int W = epd::kWidth;
    const auto &cat = images::kCategories[category_];

    Serial.printf("[grid] onEnter: category=%d count=%d\n", category_, cat.count);

    drawBackButton();
    drawHeader(cat.name);

    // 2-column thumbnail grid.  Each cell is kThumbW x kThumbH + borders.
    int cols = 2;
    int cellW = images::kThumbW + 20;   // thumbnail + padding
    int cellH = images::kThumbH + 20;
    int gridW = cols * cellW;
    int startX = (W - gridW) / 2;
    int startY = 110;

    for (int i = 0; i < cat.count; i++) {
        int col = i % cols;
        int row = i / cols;
        int x = startX + col * cellW;
        int y = startY + row * cellH;

        // Load and draw the thumbnail
        char thumbPath[96];
        const char *fname = cat.files[i];
        // Build "thumb_<name>.epd" from "<name>.epd"
        char thumbName[48];
        strncpy(thumbName, fname, sizeof(thumbName) - 1);
        thumbName[sizeof(thumbName) - 1] = '\0';
        char *dot = strchr(thumbName, '.');
        if (dot) *dot = '\0';
        snprintf(thumbPath, sizeof(thumbPath), "%s/thumb_%s.epd", cat.dir, thumbName);

        File f = LittleFS.open(thumbPath, "r");
        if (f) {
            Serial.printf("[grid] thumb %s: open OK, size=%d\n", thumbPath, f.size());
            uint8_t *buf = (uint8_t *)heap_caps_malloc(images::kThumbSize, MALLOC_CAP_SPIRAM);
            if (buf) {
                size_t got = f.read(buf, images::kThumbSize);
                f.close();
                Serial.printf("[grid] thumb %s: read %u bytes\n", thumbPath, got);
                if (got == images::kThumbSize) {
                    // Thumbnail border (drawn in logical coords — epdiy rotates these)
                    epd::fillRect(x, y, images::kThumbW + 8, images::kThumbH + 8, epd::kWhite);
                    epd::drawHLine(x, y, images::kThumbW + 8);
                    epd::drawHLine(x, y + images::kThumbH + 7, images::kThumbW + 8);
                    epd::drawVLine(x, y, images::kThumbH + 8);
                    epd::drawVLine(x + images::kThumbW + 7, y, images::kThumbH + 8);

                    // Thumbnail is pre-rotated to physical 356x200 layout.
                    // EPD_ROT_INVERTED_PORTRAIT: phys = (ly, 540 - lx - lw).
                    // Logical thumbnail is kThumbW(200) wide x kThumbH(356) tall.
                    // Physical size is swapped: 356 wide x 200 tall.
                    // physY = 540 - lx - kThumbW (logical width → physical height offset)
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
            Serial.printf("[grid] thumb %s: OPEN FAILED\n", thumbPath);
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
    if (kBackZone.hit(x, y)) return true;  // pop

    const auto &cat = images::kCategories[category_];
    int cols = 2;
    int cellW = images::kThumbW + 20;
    int cellH = images::kThumbH + 20;
    int gridW = cols * cellW;
    int startX = (epd::kWidth - gridW) / 2;
    int startY = 110;

    for (int i = 0; i < cat.count; i++) {
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

// Images are pre-rotated at build time into the physical framebuffer layout
// (960x540).  This lets us use epd_copy_to_framebuffer — a simple memcpy-like
// loop — instead of epd_draw_rotated_image, which does 519,840 per-pixel
// function calls and causes a watchdog timeout / hard freeze.
//
// epd_copy_to_framebuffer takes the area in physical coordinates and ignores
// the epdiy rotation setting.  For a full-screen image: {0, 0, 960, 540}.
constexpr int kPhysW = 960;
constexpr int kPhysH = 540;

bool GalleryViewerScreen::loadImage(int category, int index) {
    const auto &cat = images::kCategories[category];

    char path[80];
    snprintf(path, sizeof(path), "%s/%s", cat.dir, cat.files[index]);

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
    // epd_copy_to_framebuffer ignores rotation and writes directly.
    EpdRect area = {.x = 0, .y = 0, .width = kPhysW, .height = kPhysH};
    epd_copy_to_framebuffer(area, buf, epd::fb);

    Serial.printf("[viewer] copy done\n");
    free(buf);
    return true;
}

void GalleryViewerScreen::onEnter() {
    const auto &cat = images::kCategories[category_];

    Serial.printf("[viewer] onEnter: cat=%d idx=%d heap=%u psram=%u\n",
                  category_, imageIndex_,
                  ESP.getFreeHeap(), ESP.getFreePsram());

    if (!loadImage(category_, imageIndex_)) {
        Serial.println("[viewer] loadImage FAILED");
        epd::fillWhite();
        drawBackButton();
        epd::drawCenterText(&FiraSans_20, epd::kWidth / 2, 200, "No Image");
        epd::drawCenterText(&FiraSans_12, epd::kWidth / 2, 250,
                            "File not found in LittleFS");
        return;
    }

    Serial.println("[viewer] loadImage OK, drawing overlay");
    drawBackButton();

    char counter[16];
    snprintf(counter, sizeof(counter), "%d / %d", imageIndex_ + 1, cat.count);
    epd::drawCenterText(&FiraSans_12, epd::kWidth / 2, 30, counter);

    epd::drawCenterText(&FiraSans_12, epd::kWidth / 2, epd::kHeight - 20,
                        "< prev  |  next >");
    Serial.println("[viewer] onEnter done");
}

bool GalleryViewerScreen::onTouch(int x, int y) {
    int W = epd::kWidth;

    if (kBackZone.hit(x, y)) return true;  // pop

    // Left half = prev, right half = next (below the back button zone)
    if (y > 80) {
        if (x < W / 2) {
            const auto &cat = images::kCategories[category_];
            imageIndex_ = (imageIndex_ - 1 + cat.count) % cat.count;
            Serial.printf("[viewer] prev -> %d\n", imageIndex_);
        } else {
            const auto &cat = images::kCategories[category_];
            imageIndex_ = (imageIndex_ + 1) % cat.count;
            Serial.printf("[viewer] next -> %d\n", imageIndex_);
        }
        return true;  // ScreenManager::redraw() will call onEnter() + refresh()
    }
    return false;
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
        if (kBackZone.hit(x, y)) return true;  // pop
        // Check hang-up button area (large zone in lower half)
        if (y > 400) {
            endCall();
            return true;  // redraw dial pad
        }
        return false;
    }

    if (kBackZone.hit(x, y)) return true;  // pop

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
    if (kBackZone.hit(x, y)) return true;
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
    if (kBackZone.hit(x, y)) return true;

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
