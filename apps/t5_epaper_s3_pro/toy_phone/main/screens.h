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

// ---- Gallery: category list -----------------------------------------------
// Shows "Animals" and "Vehicles" buttons.  Tapping one pushes the grid.

class GalleryScreen : public ui::Screen {
public:
    void onEnter() override;
    bool onTouch(int x, int y) override;
    const char *name() const override { return "Gallery"; }

    int tappedCategory_ = -1;  // 0=animals, 1=vehicles; main.cpp reads this
};

// ---- Gallery: thumbnail grid ----------------------------------------------
// Shows a grid of image names for the selected category.  Tapping one
// pushes the viewer.

class GalleryGridScreen : public ui::Screen {
public:
    explicit GalleryGridScreen(int category) : category_(category) {}

    void onEnter() override;
    bool onTouch(int x, int y) override;
    const char *name() const override { return "GalleryGrid"; }

    int tappedImage_ = -1;  // index into the image list; main.cpp reads this
    int category_ = 0;      // 0=animals, 1=vehicles
};

// ---- Gallery: full-screen image viewer -------------------------------------
// Shows a full-screen 540x960 4bpp image.  Tap left/right halves for
// prev/next, top-left back zone to return.

class GalleryViewerScreen : public ui::Screen {
public:
    GalleryViewerScreen(int category, int imageIndex)
        : category_(category), imageIndex_(imageIndex) {}

    void onEnter() override;
    bool onTouch(int x, int y) override;
    const char *name() const override { return "Viewer"; }

private:
    int category_ = 0;
    int imageIndex_ = 0;

    bool loadImage(int category, int index);
    void next();
    void prev();
};

class PhoneScreen : public ui::Screen {
public:
    void onEnter() override;
    bool onTouch(int x, int y) override;
    const char *name() const override { return "Phone"; }
};

class ClockScreen : public ui::Screen {
public:
    void onEnter() override;
    bool onTouch(int x, int y) override;
    bool onTick() override;
    const char *name() const override { return "Clock"; }
};

class SettingsScreen : public ui::Screen {
public:
    void onEnter() override;
    bool onTouch(int x, int y) override;
    const char *name() const override { return "Settings"; }
};

}  // namespace screens
