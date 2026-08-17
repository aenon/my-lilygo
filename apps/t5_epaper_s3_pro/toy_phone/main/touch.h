// Touch — GT911 capacitive touch init and tap detection for the toy phone.
//
// GT911 must be initialized BEFORE epd_init() because touch.begin() calls
// Wire.begin() internally, and epd_init() installs its own I2C driver that
// would break subsequent Wire.begin() calls.
//
// Coordinate mapping: GT911 reports in native landscape (960x540).  We use
// the driver's built-in setSwapXY / setMirrorXY / setMaxCoordinates to remap
// to portrait (540x960).  Mirror flags may need empirical tuning per unit.

#pragma once

#include <cstdint>

namespace touch {

constexpr int kIrqPin = 3;
constexpr int kRstPin = 9;

// Initialize the GT911.  Must be called BEFORE epd::init().
// Returns true on success.
bool init();

// Non-blocking: returns true if a finger is currently on the screen.
// This is a cheap digitalRead — no I2C traffic.
bool isPressed();

// Read the current touch point.  Returns true if a touch was active and
// coordinates were read.  Coordinates are in portrait space (0..539, 0..959).
// This does an I2C read and clears the GT911 status register.
bool readPoint(int &x, int &y);

// Check if the GT911 hardware home button was pressed during the last
// readPoint() call.  Returns true if the home key event fired.
// (Only works if the panel's GT911 config has a key zone enabled.)
bool homeButtonPressed();

// Poll for a single tap.  Returns true on the press-down edge (once per tap).
// Includes a cooldown to reject bounce.  Coordinates in portrait space.
// If the GT911 home button was pressed, homePressed is set to true and the
// tap coordinates are still valid (the home key is reported alongside coords).
// Call this in loop(); it handles edge detection internally.
bool pollTap(int &x, int &y, bool &homePressed);

}  // namespace touch
