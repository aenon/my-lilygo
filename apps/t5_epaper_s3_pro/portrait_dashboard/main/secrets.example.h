// WiFi credentials.
//
// HOW TO USE:
//   cp secrets.example.h secrets.h
//   <edit secrets.h with your real creds>
//
// secrets.h is gitignored; this template (.example.h) is committed.

#pragma once

#ifndef WIFI_SSID
#define WIFI_SSID     "your-ssid-here"
#endif

#ifndef WIFI_PASSWORD
#define WIFI_PASSWORD "your-password-here"
#endif

// IANA TZ database string, see https://en.wikipedia.org/wiki/List_of_tz_database_time_zones
// Examples:
//   "PST8PDT,M3.2.0,M11.1.0"  - US Pacific
//   "EST5EDT,M3.2.0,M11.1.0"  - US Eastern
//   "CET-1CEST,M3.5.0,M10.5.0/3"  - Central Europe
//   "JST-9"                    - Japan, no DST
//   "CST-8"                    - China Standard Time, no DST
#ifndef TZ_INFO
#define TZ_INFO       "PST8PDT,M3.2.0,M11.1.0"
#endif
