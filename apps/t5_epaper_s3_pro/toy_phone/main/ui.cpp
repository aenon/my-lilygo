#include "ui.h"

#include "epd_wrap.h"
#include <Arduino.h>

namespace ui {

void ScreenManager::push(Screen *s) {
    if (depth_ >= kMaxDepth) {
        Serial.println("[ui] stack overflow, ignoring push");
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
    if (depth_ <= 1) return;
    depth_ = 1;  // keep only the launcher (index 0 is lock, index 1 is launcher)
    Serial.printf("[ui] home -> '%s' (depth=%d)\n",
                  stack_[depth_ - 1]->name(), depth_);
    redraw();
}

void ScreenManager::handleTouch(int x, int y) {
    if (depth_ == 0) return;
    if (current()->onTouch(x, y)) {
        redraw();
    }
}

void ScreenManager::handleHomeButton() {
    if (depth_ == 0) return;
    if (current()->onHomeButton()) {
        pop();
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
    epd::fillWhite();
    current()->onEnter();
    epd::refresh();
}

}  // namespace ui
