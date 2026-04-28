// WiFi credentials + optional weather config.
//
// HOW TO USE:
//   cp secrets.example.h secrets.h
//   <edit secrets.h with your real creds>
//
// secrets.h is gitignored; this template (.example.h) is committed.
// You can also override at build time via PlatformIO build_flags
// (-DWIFI_SSID=\"...\").

#pragma once

#ifndef WIFI_SSID
#define WIFI_SSID     "your-ssid-here"
#endif

#ifndef WIFI_PASSWORD
#define WIFI_PASSWORD "your-password-here"
#endif

// IANA TZ POSIX string.  Examples:
//   "PST8PDT,M3.2.0,M11.1.0"     - US Pacific
//   "EST5EDT,M3.2.0,M11.1.0"     - US Eastern
//   "CET-1CEST,M3.5.0,M10.5.0/3" - Central Europe
//   "JST-9"                      - Japan, no DST
//   "CST-8"                      - China Standard Time
#ifndef TZ_INFO
#define TZ_INFO       "PST8PDT,M3.2.0,M11.1.0"
#endif

// Hardcoded location override (optional).  If WEATHER_OVERRIDE_LOCATION is
// defined, the dashboard will skip IP geolocation and use these coordinates.
// Otherwise it will hit ip-api.com once per boot to discover where you are.
//
// Use decimal degrees, negative for south / west.
//
// #define WEATHER_OVERRIDE_LOCATION
// #define WEATHER_LAT  37.4221
// #define WEATHER_LON -122.0841
// #define WEATHER_LOCATION_NAME "Mountain View, CA"

// Units.  Open-Meteo accepts "celsius" / "fahrenheit" for temperature and
// "kmh" / "mph" / "ms" / "kn" for wind speed.
#ifndef WEATHER_TEMP_UNIT
#define WEATHER_TEMP_UNIT  "celsius"
#endif

#ifndef WEATHER_WIND_UNIT
#define WEATHER_WIND_UNIT  "kmh"
#endif

#ifndef WEATHER_PRECIP_UNIT
#define WEATHER_PRECIP_UNIT "mm"
#endif
