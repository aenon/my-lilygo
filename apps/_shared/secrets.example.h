// Shared WiFi + common secrets for all apps.
//
// HOW TO USE:
//   cp secrets.example.h secrets.h
//   <edit secrets.h with your real creds>
//
// secrets.h is gitignored; this template (.example.h) is committed.
// Every app picks it up automatically via the -I apps/_shared build
// flag in platformio.ini.

#pragma once

// ── WiFi ──────────────────────────────────────────────────────────────
#ifndef WIFI_SSID
#define WIFI_SSID     "your-ssid-here"
#endif

#ifndef WIFI_PASSWORD
#define WIFI_PASSWORD "your-password-here"
#endif
