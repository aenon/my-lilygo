#include "touch.h"

#include <Arduino.h>
#include <Wire.h>
#include <TouchDrvGT911.hpp>

namespace touch {

static TouchDrvGT911 g_touch;
static bool g_homePressed = false;
static bool g_wasPressed = false;
static uint32_t g_lastTapMs = 0;

// Coordinate mapping: the GT911 on this board is configured to report in
// portrait (540x960) natively, so no XY swap is needed.  Mirror flags may
// need empirical tuning — tap the four corners and compare with expected
// screen positions.
static constexpr bool kSwapXY  = false;
static constexpr bool kMirrorX = false;
static constexpr bool kMirrorY = false;

bool init() {
    g_touch.setPins(kRstPin, kIrqPin);

    if (!g_touch.begin(Wire, GT911_SLAVE_ADDRESS_L, 39, 40)) {
        Serial.println("[touch] GT911 init FAILED");
        return false;
    }

    Serial.println("[touch] GT911 init OK");

    int16_t resX, resY;
    g_touch.getResolution(&resX, &resY);
    Serial.printf("[touch] native resolution: %dx%d\n", resX, resY);

    // GT911 on this board already reports in portrait (540x960).
    // Set max coordinates so mirror flags (if any) can take effect.
    g_touch.setSwapXY(kSwapXY);
    g_touch.setMaxCoordinates(540, 960);
    g_touch.setMirrorXY(kMirrorX, kMirrorY);

    // Idle HIGH, goes LOW while touched — ideal for polling.
    g_touch.setInterruptMode(LOW_LEVEL_QUERY);

    // Register home button callback (fires inside getPoint when key zone
    // is touched, if the panel supports it).
    g_touch.setHomeButtonCallback([](void *) {
        Serial.println("[touch] home button pressed");
        g_homePressed = true;
    }, nullptr);

    return true;
}

bool isPressed() {
    return g_touch.isPressed();
}

bool readPoint(int &x, int &y) {
    g_homePressed = false;
    int16_t tx[5], ty[5];
    uint8_t n = g_touch.getPoint(tx, ty, 1);
    if (n > 0) {
        x = tx[0];
        y = ty[0];
        return true;
    }
    return false;
}

bool homeButtonPressed() {
    return g_homePressed;
}

bool pollTap(int &x, int &y) {
    bool pressed = isPressed();

    // Edge detection: fire only on press-down
    if (pressed && !g_wasPressed) {
        uint32_t now = millis();
        if (now - g_lastTapMs > 200) {  // 200ms cooldown
            g_lastTapMs = now;
            if (readPoint(x, y)) {
                Serial.printf("[touch] tap at (%d, %d)\n", x, y);
                g_wasPressed = true;
                return true;
            }
        }
    }

    if (!pressed) {
        g_wasPressed = false;
    }

    return false;
}

}  // namespace touch
