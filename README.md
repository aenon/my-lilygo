# My LilyGo

Source: [github.com/aenon/my-lilygo](https://github.com/aenon/my-lilygo)

Personal firmware for my LilyGo dev boards. Currently scoped to two devices,
both ESP32-S3 based:

| Device | Board ID | Notes |
|---|---|---|
| **T5 E-Paper S3 Pro** | `H752-02` | 4.7" 960×540 16-gray e-paper, 915 MHz LoRa, GPS/GLONASS/BeiDou |
| **T-Dongle S3** | `K193` | USB-A stick with a 0.96" LCD (apps TBD) |

A third board, the **T-Echo (nRF52840)**, lives in the same drawer but runs
[Meshtastic](https://meshtastic.org/) firmware unmodified — not built from
this repo.

## Repository layout

```
apps/
  _shared/                       # cross-device libs (net, fonts, etc.) — TBD
  t5_epaper_s3_pro/
    sanity_check/main/           # I2C + GPS hardware probe
    gps_dashboard/main/          # Landscape GPS/GNSS dashboard
    portrait_dashboard/main/     # Portrait clock + WiFi + battery + system
    weather_dashboard/main/      # Portrait Open-Meteo weather dashboard
  t_dongle_s3/                   # placeholder, no apps yet
scripts/
  bootstrap-vendor.sh            # clones LilyGo vendor repos into vendor/
vendor/                          # gitignored — populated by bootstrap script
platformio.ini                   # one [env:*] per device
```

`secrets.h` files are gitignored everywhere; commit only `secrets.example.h`.

## Setup

```bash
git clone git@github.com:aenon/my-lilygo.git
cd my-lilygo

# HTTPS: git clone https://github.com/aenon/my-lilygo.git

# Fetch upstream vendor repos (board JSONs, epdiy, BQ27220, SensorLib, ...).
# Idempotent; re-run to pull updates.
scripts/bootstrap-vendor.sh

# For apps that need WiFi, copy the template and edit:
cp apps/t5_epaper_s3_pro/weather_dashboard/main/secrets.example.h \
   apps/t5_epaper_s3_pro/weather_dashboard/main/secrets.h
$EDITOR apps/t5_epaper_s3_pro/weather_dashboard/main/secrets.h
```

## Build / flash / monitor

Pick which app to build by uncommenting one `build_src_filter =` line in the
relevant `[env:*]` block of `platformio.ini`. Default env is
`t5_epaper_s3_pro` building `weather_dashboard`.

```bash
pio run -e t5_epaper_s3_pro                    # compile
pio run -e t5_epaper_s3_pro -t upload          # flash via USB-C
pio device monitor --rts 0 --dtr 0
```

`--rts 0 --dtr 0` is needed because the native USB-CDC interface re-enumerates
on hard reset, and DTR/RTS toggling can cause the monitor to disconnect.

## T5 E-Paper S3 Pro hardware reference

- ESP32-S3R8 (8 MB OPI PSRAM, 16 MB QIO flash, native USB-C)
- ED047TC1 4.7" 960×540, 16 grayscale, parallel epdiy interface
- TPS65185 EPD PMIC (`0x68`) + PCA9535 / XL9555 I/O expander (`0x20`)
- GT911 capacitive touch (`0x5D`)
- PCF85063 RTC (`0x51`)
- BQ27220 fuel gauge (`0x55`) + BQ25896 charger (`0x6B`)
- Semtech SX1262 LoRa @ 915 MHz (US/AU/NZ ISM band only — **don't transmit
  in EU/Asia without changing the radio frequency**)
- Quectel L76K or u-blox MIA-M10Q GNSS, UART2 @ 9600 baud
- I2C bus on **SDA=39, SCL=40** (the obvious-looking 17/18 pair is the EPD
  data bus — do not poke it)

### Working notes

- **Always call `Wire.begin(39, 40)` before `epd_init()`.** Otherwise epdiy
  installs the ESP-IDF I2C driver itself, and any later `Wire.begin()` will
  fail and leave Wire's TX buffer NULL — silently breaking every BQ27220 /
  XL9555 transaction.
- **Don't transmit on the SX1262 without an antenna attached.** Hot-plugging
  or transmitting bare can damage the PA.
- **Natural portrait orientation on this PCB is `EPD_ROT_INVERTED_PORTRAIT`**
  (puts the USB-C connector at the bottom).
- **`epd_poweroff()` after every refresh.** Leaving the panel powered will
  burn it in.
- **`weather_dashboard` refresh policy:** weather fetch on success → next
  attempt at top of next hour; on failure → retry in 60 s. Display redraws
  on success and once per minute (clock tick) — independent of fetch state.

## Credits

Built on top of the upstream LilyGo vendor reference firmware:

- T5 E-Paper S3 Pro:
  <https://github.com/Xinyuan-LilyGO/T5S3-4.7-e-paper-PRO> (branch `H752-01`)
- T-Dongle S3:
  <https://github.com/Xinyuan-LilyGO/T-Dongle-S3>

Their `lib/` and `boards/` directories are referenced via `lib_extra_dirs`
and `boards_dir` rather than copied into this tree.
