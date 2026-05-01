# LilyGo T-Dongle S3 (K193)

## Apps

### `ticker_clock_weather`

**Static** three-line **160×80** layout: date/time, place name, temperature + short condition. Data from [Open-Meteo](https://open-meteo.com/) and [ip-api.com](http://ip-api.com/) (no API keys). The screen **redraws only when the text changes** (e.g. minute rollover or new weather), so it stays visually stable.

```bash
cp apps/t_dongle_s3/ticker_clock_weather/main/secrets.example.h \
   apps/t_dongle_s3/ticker_clock_weather/main/secrets.h
pio run -e t_dongle_s3 -t upload
pio device monitor --rts 0 --dtr 0
```

Uses **TFT_eSPI** with LilyGO’s vendor `Setup47_ST7735` (`lib_extra_dirs = vendor/T-Dongle-S3/lib`). Weather refresh: ~every **hour** on success, **60 s** retry on failure (keeps HTTP and RAM light on a no-PSRAM board). The LCD is **not continuously repainted**; only full clears when the displayed lines change.

### `vertical_color_clock`

**Portrait** (80×160). **USB-A at the bottom** — `kTftRotation` in `main.cpp` (default **0**). HH: and MM use the **largest TFT_eSPI font that fits** the 80px width and the space left above the date block (tries **6 → 7 → 4 → 2×2 → 2**). Date lines stay on font 2.

```bash
cp apps/t_dongle_s3/vertical_color_clock/main/secrets.example.h \
   apps/t_dongle_s3/vertical_color_clock/main/secrets.h
pio run -e t_dongle_s3_vertical_clock -t upload
```

If the clock is upside-down, set `kTftRotation` to **2** in `main.cpp`.

---

## Display capabilities

Upstream hardware/UI reference: [Xinyuan-LilyGO/T-Dongle-S3](https://github.com/Xinyuan-LilyGO/T-Dongle-S3) (vendored under `vendor/T-Dongle-S3` after `./scripts/bootstrap-vendor.sh`).

### Panel summary

| Attribute | Detail |
|-----------|--------|
| Controller | **ST7735** (vendor driver: `esp_lcd` ST7735 panel + SPI) |
| **Active pixels (in vendor demos)** | **160 × 80** RGB565 — see `examples/lcd/lcd.ino` and `examples/TFT_eSPI/TFT_eSPI.ino` (`pushImage(..., 160, 80, ...)`) |
| Marketing / physical | Often described as **0.96″**; **80 × 160** is the same panel in the other orientation (portrait vs landscape depends on `swap_xy` / `setRotation`) |
| Color | **16 bpp RGB565**; vendor init uses **BGR** element order (`LCD_RGB_ELEMENT_ORDER_BGR`) |
| Backlight | **GPIO 38**, PWM-dimmable (see `lcd.ino` `LEDC_*` and `TFT_LEDA_PIN`) |

So on this small screen you are drawing into a **roughly thumbnail-sized** raster: about **12.8k pixels** (~25.6 KB for a full RGB565 framebuffer). There is **no PSRAM** on the base T-Dongle-S3 SKU (per LilyGO quick start), so keep full-screen buffers and UI complexity modest.

### What you can display (practically)

1. **Full-screen bitmaps** — RGB565 buffers or embedded C arrays (`image.h`-style), pushed with `esp_lcd_panel_draw_bitmap` or `TFT_eSPI::pushImage`.
2. **Solid fills and 2D primitives** — through **TFT_eSPI** (`fillScreen`, rectangles, lines, etc.) where enabled in the driver setup.
3. **Text** — built-in fonts in **TFT_eSPI** (`setTextFont`, `setTextColor`, `println`, …) or **LVGL** fonts.
4. **Simple animation** — redraw regions or full screen at modest frame rates; avoid large double buffers without PSRAM.
5. **Rich UI** — **LVGL 9** example under `examples/lvgl9/` (widgets, themes, at cost of flash/RAM).
6. **Decoded images** — JPEG/etc. is possible via a decoder writing into RGB565, but you must manage RAM; many sketches use **preconverted** assets instead.

### Other on-board outputs (not the LCD)

- **APA102 RGB LED** — data/clock on **GPIO 40 / 39** in `TFT_eSPI.ino` (`LED_DI_PIN`, `LED_CI_PIN`); usable independently of the TFT for status/blink patterns.

### SPI pinout (TFT, from vendor examples)

| Function | GPIO |
|----------|------|
| MOSI | 3 |
| SCK | 5 |
| CS | 4 |
| DC | 2 |
| RST | 1 |
| Backlight | 38 |

Exact numbers belong in driver init once; keep them in sync with the **Dual / Plus** quick starts if you use a variant board.

### Where to look in `vendor/T-Dongle-S3/`

| Example path | What it shows |
|--------------|----------------|
| `examples/lcd/` | `esp_lcd` ST7735 init, gaps/mirror/swap for alignment, RGB fills, bitmaps from headers |
| `examples/TFT_eSPI/` | Bodmer TFT_eSPI, `setRotation(1)`, image cycling with `pushImage` |
| `examples/factory_screen/` | LVGL factory-style UI on the same panel |
| `examples/lvgl9/` | Newer LVGL + ST7735 bring-up |

Use these as the ground truth for resolution and rotation before adding apps under `apps/t_dongle_s3/<app>/main/`.
