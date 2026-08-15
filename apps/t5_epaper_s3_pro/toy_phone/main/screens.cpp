#include "screens.h"

#include "epd_wrap.h"
#include "firasans_12.h"
#include "firasans_20.h"
#include "images.h"

#include <Arduino.h>
#include <LittleFS.h>
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

    drawBackButton();
    drawHeader(cat.name);

    // Grid: 1 column of large buttons, each showing the image filename
    int btnW = W - 80;
    int btnH = 100;
    int btnX = 40;
    int startY = 110;
    int gap = 20;

    for (int i = 0; i < cat.count; i++) {
        int y = startY + i * (btnH + gap);

        epd::fillRect(btnX, y, btnW, btnH, epd::kWhite);
        epd::drawHLine(btnX, y, btnW);
        epd::drawHLine(btnX, y + btnH - 1, btnW);
        epd::drawVLine(btnX, y, btnH);
        epd::drawVLine(btnX + btnW - 1, y, btnH);

        // Strip .epd extension for display
        char label[32];
        strncpy(label, cat.files[i], sizeof(label) - 1);
        label[sizeof(label) - 1] = '\0';
        char *dot = strchr(label, '.');
        if (dot) *dot = '\0';

        // Capitalize first letter
        if (label[0] >= 'a' && label[0] <= 'z') label[0] -= 32;

        epd::drawCenterText(&FiraSans_20, W / 2, y + 40, label);
        char num[8];
        snprintf(num, sizeof(num), "#%d", i + 1);
        epd::drawCenterText(&FiraSans_12, W / 2, y + 70, num);
    }

    epd::drawCenterText(&FiraSans_12, W / 2, epd::kHeight - 30,
                        "Tap to view  |  < back");
}

bool GalleryGridScreen::onTouch(int x, int y) {
    if (kBackZone.hit(x, y)) return true;  // pop

    const auto &cat = images::kCategories[category_];
    int btnW = epd::kWidth - 80;
    int btnH = 100;
    int btnX = 40;
    int startY = 110;
    int gap = 20;

    for (int i = 0; i < cat.count; i++) {
        int by = startY + i * (btnH + gap);
        ui::TapZone zone = {btnX, by, btnW, btnH};
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

constexpr int kImageSize = 540 * 960 / 2;  // 259,200 bytes (4bpp packed)

// Image data is stored pre-rotated in physical 960x540 orientation so we can
// use the fast epd_copy_to_framebuffer.  But epd_copy_to_framebuffer writes
// in physical coords and ignores rotation — it would place the image in the
// wrong orientation.  For portrait we use epd_draw_rotated_image which
// handles the rotation per-pixel (slower but correct).
//
// Images on disk are 540x960 portrait, 4bpp packed (left=low nibble).
constexpr int kImgLogicalW = 540;
constexpr int kImgLogicalH = 960;

bool GalleryViewerScreen::loadImage(int category, int index) {
    const auto &cat = images::kCategories[category];

    char path[80];
    snprintf(path, sizeof(path), "%s/%s", cat.dir, cat.files[index]);

    File f = LittleFS.open(path, "r");
    if (!f) {
        Serial.printf("[viewer] failed to open %s\n", path);
        return false;
    }

    uint8_t *buf = (uint8_t *)malloc(kImageSize);
    if (!buf) {
        Serial.println("[viewer] malloc failed");
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

    Serial.printf("[viewer] loaded %s (%u bytes)\n", path, got);

    epd::fillWhite();

    // Draw the image full-screen.  epd_draw_rotated_image applies the
    // rotation transform so our 540x960 portrait data maps correctly
    // into the 960x540 physical framebuffer.
    EpdRect area = {.x = 0, .y = 0, .width = kImgLogicalW, .height = kImgLogicalH};
    epd_draw_rotated_image(area, buf, epd::fb);

    free(buf);
    return true;
}

void GalleryViewerScreen::onEnter() {
    const auto &cat = images::kCategories[category_];

    if (!loadImage(category_, imageIndex_)) {
        // Fallback: show error text
        epd::fillWhite();
        drawBackButton();
        epd::drawCenterText(&FiraSans_20, epd::kWidth / 2, 200, "No Image");
        epd::drawCenterText(&FiraSans_12, epd::kWidth / 2, 250,
                            "File not found in LittleFS");
        return;
    }

    // Draw nav overlay: back button + arrows
    drawBackButton();

    // Image counter at top center
    char counter[16];
    snprintf(counter, sizeof(counter), "%d / %d", imageIndex_ + 1, cat.count);
    epd::drawCenterText(&FiraSans_12, epd::kWidth / 2, 30, counter);

    // Bottom hint
    epd::drawCenterText(&FiraSans_12, epd::kWidth / 2, epd::kHeight - 20,
                        "< prev  |  next >");
}

bool GalleryViewerScreen::onTouch(int x, int y) {
    int W = epd::kWidth;

    if (kBackZone.hit(x, y)) return true;  // pop

    // Left half = prev, right half = next (below the back button zone)
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
    const auto &cat = images::kCategories[category_];
    imageIndex_ = (imageIndex_ + 1) % cat.count;
    Serial.printf("[viewer] next -> %d\n", imageIndex_);
    onEnter();
}

void GalleryViewerScreen::prev() {
    const auto &cat = images::kCategories[category_];
    imageIndex_ = (imageIndex_ - 1 + cat.count) % cat.count;
    Serial.printf("[viewer] prev -> %d\n", imageIndex_);
    onEnter();
}

// ===========================================================================
// Placeholder screens (later PRs will fill these in)
// ===========================================================================

static void drawPlaceholder(const char *title, const char *hint) {
    int W = epd::kWidth;
    int H = epd::kHeight;

    drawBackButton();
    drawHeader(title);

    epd::drawCenterText(&FiraSans_12, W / 2, H / 2, hint);
    epd::drawCenterText(&FiraSans_12, W / 2, H - 30, "Tap < to go back");
}

void PhoneScreen::onEnter() {
    drawPlaceholder("Phone", "Coming soon");
}
bool PhoneScreen::onTouch(int x, int y) {
    return kBackZone.hit(x, y);
}

void ClockScreen::onEnter() {
    drawPlaceholder("Clock", "Coming soon");
}
bool ClockScreen::onTouch(int x, int y) {
    return kBackZone.hit(x, y);
}
bool ClockScreen::onTick() {
    return false;
}

void SettingsScreen::onEnter() {
    drawPlaceholder("Settings", "Coming soon");
}
bool SettingsScreen::onTouch(int x, int y) {
    return kBackZone.hit(x, y);
}

}  // namespace screens
