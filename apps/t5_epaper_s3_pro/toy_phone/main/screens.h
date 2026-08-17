// Screens — all screen implementations for the toy phone.

#pragma once

#include "ui.h"

namespace screens {

// ---- Lock Screen -----------------------------------------------------------
// Full-screen "tap to unlock" — tap anywhere to proceed to the launcher.

class LockScreen : public ui::Screen {
public:
    void onEnter() override;
    bool onTouch(int x, int y) override;
    bool onHomeButton() override { return false; }  // no home on lock screen
    const char *name() const override { return "Lock"; }
};

// ---- Launcher --------------------------------------------------------------
// 2x2 icon grid: Phone, Gallery, Clock, Settings.
// Tapping an icon pushes the corresponding screen.

class LauncherScreen : public ui::Screen {
public:
    void onEnter() override;
    bool onTouch(int x, int y) override;
    const char *name() const override { return "Launcher"; }

    // Set by onTouch when an icon is tapped; main.cpp reads this to decide
    // which screen to push.  -1 = no tap.
    int tappedIcon_ = -1;

private:
    static constexpr int kCols = 2;
    static constexpr int kRows = 2;
    static constexpr int kIconSize = 180;
    static constexpr int kGap = 40;
    static constexpr int kMarginTop = 220;
    static constexpr int kMarginX = 40;

    struct Icon {
        const char *label;
        int x, y;  // top-left of icon cell
    };

    void getIcons(Icon *icons) const;
};

// ---- Back button tap zone (shared by all sub-screens) ---------------------
// Top-left 80x80 area.  Used by screens that have a "back" affordance.

// ---- Gallery: thumbnail grid ----------------------------------------------
// Flat grid of all photos.  Tapping one pushes the viewer.

class GalleryGridScreen : public ui::Screen {
public:
    void onEnter() override;
    bool onTouch(int x, int y) override;
    const char *name() const override { return "Gallery"; }

    int tappedImage_ = -1;  // index into images::kImages; main.cpp reads this
};

// ---- Gallery: full-screen image viewer -------------------------------------
// Shows a full-screen 540x960 4bpp image.  Tap left/right halves for
// prev/next, top-left back zone to return.

class GalleryViewerScreen : public ui::Screen {
public:
    explicit GalleryViewerScreen(int imageIndex) : imageIndex_(imageIndex) {}

    void onEnter() override;
    bool onTouch(int x, int y) override;
    const char *name() const override { return "Viewer"; }

private:
    int imageIndex_ = 0;

    bool loadImage(int index);
    void next();
    void prev();
};

// ---- Phone: dial pad + mock call screen -----------------------------------

class PhoneScreen : public ui::Screen {
public:
    void onEnter() override;
    bool onTouch(int x, int y) override;
    const char *name() const override { return "Phone"; }

    // When true, the screen shows the "Calling..." mock instead of the dial pad.
    bool calling_ = false;

private:
    char dialed_[16] = "";
    int dialedLen_ = 0;

    void pressKey(char c);
    void deleteKey();
    void startCall();
    void endCall();
};

// ---- Clock: big digital time + date (NTP) ---------------------------------

class ClockScreen : public ui::Screen {
public:
    void onEnter() override;
    bool onTouch(int x, int y) override;
    bool onTick() override;
    const char *name() const override { return "Clock"; }

private:
    uint32_t lastRenderMs_ = 0;
    int lastMinute_ = -1;
    void drawClock();
};

// ---- Settings: visual-only toggle rows ------------------------------------

class SettingsScreen : public ui::Screen {
public:
    void onEnter() override;
    bool onTouch(int x, int y) override;
    const char *name() const override { return "Settings"; }

private:
    static constexpr int kRowCount = 3;
    bool toggles_[kRowCount] = {true, false, true};
    const char *labels_[kRowCount] = {"Sound", "Dark Mode", "24 Hour"};
    int tappedToggle_ = -1;
};

}  // namespace screens
