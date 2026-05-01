// Copy to secrets.h and edit. secrets.h is gitignored.
#pragma once

#ifndef WIFI_SSID
#define WIFI_SSID "your-ssid-here"
#endif

#ifndef WIFI_PASSWORD
#define WIFI_PASSWORD "your-password-here"
#endif

// IANA TZ for local clock. Examples:
//   "PST8PDT,M3.2.0,M11.1.0"
//   "EST5EDT,M3.2.0,M11.1.0"
#ifndef TZ_INFO
#define TZ_INFO "PST8PDT,M3.2.0,M11.1.0"
#endif

// #define WEATHER_OVERRIDE_LOCATION
// #define WEATHER_LAT  37.4221
// #define WEATHER_LON -122.0841
// #define WEATHER_LOCATION_NAME "Mountain View, CA"

#ifndef WEATHER_TEMP_UNIT
#define WEATHER_TEMP_UNIT "celsius"
#endif
