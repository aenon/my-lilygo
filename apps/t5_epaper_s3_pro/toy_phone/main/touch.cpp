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

// Use the RST pin like the factory example does.  GPIO 9 is shared with
// the EPD data bus (D8), but the factory example works — the GT911 only
// needs the RST pin during init to set the I2C address.  After init, the
// EPD's GPIO 9 toggling doesn't affect the GT911's operation because the
// GT911 is already configured and running.
bool init() {
    // Release any GPIO hold on the RST pin (from deep sleep or boot)
    gpio_hold_dis((gpio_num_t)kRstPin);

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
    // Not used — pollTap calls getPoint() directly which reads coordinates
    // AND clears the buffer in one I2C transaction.  This avoids the IRQ
    // pin entirely (GPIO 3 can be unreliable when the EPD drives GPIO 9,
    // which is shared between GT911 RST and EPD D8).
    return false;
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

bool healthCheck() {
    // Try to read the chip ID — a quick I2C read that should always work
    // if the GT911 is alive.  Returns 911 on success.
    uint32_t id = g_touch.getChipID();
    bool ok = (id == 911);
    if (!ok) {
        Serial.printf("[touch] health check FAILED (chipID=%lu)\n", id);
    }
    return ok;
}

bool reinit() {
    Serial.println("[touch] re-initializing GT911 (hardware reset)...");
    // Hardware reset: toggle RST pin (GPIO 9).  This may glitch the EPD
    // momentarily, but the next refresh will recover.
    pinMode(kRstPin, OUTPUT);
    digitalWrite(kRstPin, LOW);
    delay(10);
    digitalWrite(kRstPin, HIGH);
    delay(100);  // GT911 needs ~50-100ms to boot after reset
    return init();
}

bool pollTap(int &x, int &y, bool &homePressed) {
    homePressed = false;

    // Read the GT911 directly via I2C.  getPoint() reads coordinates AND
    // clears the buffer in one transaction.  This avoids the IRQ pin entirely
    // (GPIO 3 can be unreliable when the EPD drives GPIO 9, which is shared
    // between GT911 RST and EPD D8).
    int16_t tx[1], ty[1];
    g_homePressed = false;
    uint8_t n = g_touch.getPoint(tx, ty, 1);
    bool pressed = (n > 0);

    // Log raw GT911 state every 2 seconds for debugging
    static uint32_t lastLog = 0;
    if (millis() - lastLog > 2000) {
        lastLog = millis();
        Serial.printf("[touch] n=%d pressed=%d wasPressed=%d irq=%d chipID=%lu\n",
                      n, pressed, g_wasPressed, digitalRead(kIrqPin),
                      g_touch.getChipID());
        Serial.flush();
    }

    if (pressed) {
        // Capture home button state (set by the callback inside getPoint).
        homePressed = g_homePressed;
        x = tx[0];
        y = ty[0];
    }

    // Edge detection: fire only on press-down
    if (pressed && !g_wasPressed) {
        uint32_t now = millis();
        if (now - g_lastTapMs > 200) {  // 200ms cooldown
            g_lastTapMs = now;
            Serial.printf("[touch] tap at (%d, %d)%s\n", x, y,
                          homePressed ? " +HOME" : "");
            g_wasPressed = true;
            return true;
        }
    }

    if (!pressed) {
        g_wasPressed = false;
    }

    return false;
}

}  // namespace touch
