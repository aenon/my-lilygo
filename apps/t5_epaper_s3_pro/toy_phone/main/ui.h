// UI framework — Screen base class, ScreenManager with a screen stack,
// and tap-zone geometry helpers for the toy phone.

#pragma once

#include <cstdint>

namespace ui {

// A tap zone is a rectangle on the portrait screen (540x960).
struct TapZone {
    int x, y, w, h;
    bool hit(int px, int py) const {
        return px >= x && px < x + w && py >= y && py < y + h;
    }
};

// Screen interface.  Each screen draws itself and handles touch events.
class Screen {
public:
    virtual ~Screen() = default;

    // Called when this screen becomes the top of the stack.
    // Draw the screen content into the framebuffer here.
    virtual void onEnter() = 0;

    // Called when the user taps at (x, y) in portrait coordinates.
    // Return true if the tap was consumed (triggers a refresh).
    virtual bool onTouch(int x, int y) = 0;

    // Optional: called when the hardware home button is pressed.
    // Default: return true to trigger a pop.
    virtual bool onHomeButton() { return false; }

    // Optional: called periodically in loop() (every ~1s).
    // Return true to trigger a refresh.
    virtual bool onTick() { return false; }

    virtual const char *name() const = 0;
};

// ScreenManager owns a small stack of screens.  The top screen is active.
// Max depth: lock -> launcher -> gallery -> grid -> viewer = 5.
class ScreenManager {
public:
    static constexpr int kMaxDepth = 6;

    void push(Screen *s);
    void pop();
    void home();  // pop all the way to index 1 (launcher)

    Screen *current() const { return stack_[depth_ - 1]; }
    int depth() const { return depth_; }

    void handleTouch(int x, int y);
    void handleHomeButton();
    void tick();

private:
    Screen *stack_[kMaxDepth];  // NOLINT: size set by kMaxDepth above
    int depth_ = 0;

    void redraw();
};

}  // namespace ui
