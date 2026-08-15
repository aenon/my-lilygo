// Image list — declares the available images in LittleFS for each category.
// Images are 540x960 4bpp raw files (259,200 bytes each).
//
// To add images:
//   1. Put source PNG/SVG in images/<category>/
//   2. Run: python3 scripts/svg-to-epaper.py images/<cat>/<file>.png \
//             data/images/<cat>/<file>.epd --width 540 --height 960
//   3. Add the filename (without path) to the array below
//   4. pio run -e t5_epaper_s3_pro -t uploadfs

#pragma once

namespace images {

struct Category {
    const char *name;           // display label
    const char *dir;            // LittleFS directory, e.g. "/images/animals"
    const char *const *files;   // filenames within dir
    int count;
};

// Animals
static constexpr const char *kAnimals[] = {
    "elephant.epd",
    "giraffe.epd",
    "cat.epd",
};
static constexpr int kAnimalsCount = 3;

// Vehicles
static constexpr const char *kVehicles[] = {
    "excavator.epd",
    "bulldozer.epd",
    "tractor.epd",
};
static constexpr int kVehiclesCount = 3;

static constexpr Category kCategories[] = {
    {"Animals",  "/images/animals",  kAnimals,  kAnimalsCount},
    {"Vehicles", "/images/vehicles", kVehicles, kVehiclesCount},
};
static constexpr int kCategoryCount = 2;

}  // namespace images
