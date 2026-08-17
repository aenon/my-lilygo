// Image list — flat list of all photos in LittleFS.
// Full images are 540x960 4bpp raw, pre-rotated to physical 960x540 (259,200 bytes).
// Thumbnails are 240x240 4bpp raw, pre-rotated to physical 240x240 (28,800 bytes).
//
// To add images:
//   1. Put source PNG in images/
//   2. Convert full: python3 scripts/svg-to-epaper.py images/<file>.png \
//          data/images/<file>.epd --width 540 --height 960
//   3. Generate thumbnail: python3 scripts/gen-thumbnails.py
//   4. Add the filename to kImages below
//   5. pio run -e t5_epaper_s3_pro -t uploadfs

#pragma once

namespace images {

constexpr int kFullW = 540;
constexpr int kFullH = 960;
constexpr int kFullSize = kFullW * kFullH / 2;  // 259,200 bytes

constexpr int kThumbW = 240;
constexpr int kThumbH = 240;
constexpr int kThumbSize = kThumbW * kThumbH / 2;  // 28,800 bytes

constexpr const char *kDir = "/images";

static constexpr const char *kImages[] = {
    "elephant.epd",
    "giraffe.epd",
    "cat.epd",
    "excavator.epd",
    "bulldozer.epd",
    "tractor.epd",
};
static constexpr int kImageCount = 6;

}  // namespace images
