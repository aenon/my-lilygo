// Image list — declares the available images in LittleFS for each category.
// Full images are 540x960 4bpp raw (259,200 bytes).
// Thumbnails are 200x356 4bpp raw (35,600 bytes).
//
// To add images:
//   1. Put source PNG in images/<category>/
//   2. Convert full: python3 scripts/svg-to-epaper.py images/<cat>/<file>.png \
//          data/images/<cat>/<file>.epd --width 540 --height 960
//   3. Generate thumbnail (see scripts/gen-thumbnails.py)
//   4. Add the filename to the array below
//   5. pio run -e t5_epaper_s3_pro -t uploadfs

#pragma once

namespace images {

constexpr int kFullW = 540;
constexpr int kFullH = 960;
constexpr int kFullSize = kFullW * kFullH / 2;  // 259,200 bytes

constexpr int kThumbW = 200;
constexpr int kThumbH = 356;
constexpr int kThumbSize = kThumbW * kThumbH / 2;  // 35,600 bytes

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
