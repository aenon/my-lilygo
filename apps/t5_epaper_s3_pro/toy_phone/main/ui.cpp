#include "ui.h"

#include "epd_wrap.h"
#include "touch.h"
#include <Arduino.h>

namespace ui {

// Suppress touch for this long after a refresh to drain queued taps.
static uint32_t g_suppressUntil = 0;

void ScreenManager::push(Screen *s) {
    if (depth_ >= kMaxDepth) {
        Serial.printf("[ui] STACK OVERFLOW: depth=%d max=%d, ignoring push of '%s'\n",
                      depth_, kMaxDepth, s->name());
        return;
    }
    stack_[depth_++] = s;
    Serial.printf("[ui] push '%s' (depth=%d)\n", s->name(), depth_);
    redraw();
}

void ScreenManager::pop() {
    if (depth_ <= 1) {
        Serial.println("[ui] can't pop last screen");
        return;
    }
    depth_--;
    Serial.printf("[ui] pop -> '%s' (depth=%d)\n",
                  stack_[depth_ - 1]->name(), depth_);
    redraw();
}

void ScreenManager::home() {
    if (depth_ <= 2) return;  // already at launcher or lock
    depth_ = 2;  // keep lock (0) + launcher (1)
    Serial.printf("[ui] home -> '%s' (depth=%d)\n",
                  stack_[depth_ - 1]->name(), depth_);
    redraw();
}

void ScreenManager::suppressTouch(uint32_t ms) {
    g_suppressUntil = millis() + ms;
    // Drain any pending GT911 touch data so stale coordinates don't leak
    // through when suppression expires.
    int dx, dy;
    touch::readPoint(dx, dy);
}

void ScreenManager::handleTouch(int x, int y) {
    if (depth_ == 0) return;

    // Suppress touches queued during the refresh period.
    if (millis() < g_suppressUntil) return;

    // Global back zone: top-left corner, only when below the launcher.
    if (depth_ > 2 && kBackZone.hit(x, y)) {
        Serial.printf("[ui] back zone hit -> pop\n");
        pop();
        return;
    }

    if (current()->onTouch(x, y)) {
        redraw();
    }
}

void ScreenManager::handleHomeButton() {
    if (depth_ == 0) return;
    if (millis() < g_suppressUntil) return;  // same suppression as touch
    if (current()->onHomeButton()) {
        home();
    }
}

void ScreenManager::tick() {
    if (depth_ == 0) return;
    if (current()->onTick()) {
        redraw();
    }
}

void ScreenManager::redraw() {
    if (depth_ == 0) return;
    Serial.printf("[ui] redraw '%s'\n", current()->name());
    uint32_t t0 = millis();
    epd::fillWhite();
    current()->onEnter();
    epd::refresh();
    Serial.printf("[ui]   refresh took %lums\n", millis() - t0);
    // Suppress touches for 300ms after refresh to drain any queued taps
    // that were made during the ~2s EPD update.
    suppressTouch(300);
}

}  // namespace ui
