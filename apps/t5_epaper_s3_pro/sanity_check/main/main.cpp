// Hardware sanity-check sketch for the LilyGo T5 E-Paper S3 Pro (H752-02).
//
// Pin map taken from the official vendor repo, branch H752-01:
//   https://github.com/Xinyuan-LilyGO/T5S3-4.7-e-paper-PRO/tree/H752-01
//
// What it does on boot:
//   1. Prints chip / flash / PSRAM info over USB CDC serial.
//   2. Scans the shared I2C bus (SDA=39, SCL=40) and labels every device it
//      finds with the corresponding chip name.
//   3. Listens on UART2 for ~5 seconds for L76K / MIA-M10Q GPS NMEA sentences.
//   4. Loops printing a heartbeat once per second.
//
// Expected I2C devices on a healthy H752-02:
//   - 0x20  PCA9535PW   I/O expander (drives EPD power-up, buttons, Vcom)
//   - 0x51  PCF85063    real-time clock
//   - 0x55  BQ27220     battery fuel gauge
//   - 0x5D  GT911       capacitive touch controller
//   - 0x68  TPS65185    e-paper power management chip
//   - 0x6B  BQ25896     battery charger

#include <Arduino.h>
#include <Wire.h>

namespace {

constexpr uint8_t kI2cSda      = 39;
constexpr uint8_t kI2cScl      = 40;
constexpr uint32_t kI2cFreq    = 100000;

constexpr uint8_t kGpsRxFromGps = 44;  // ESP32 RX  <-  GPS TX
constexpr uint8_t kGpsTxToGps   = 43;  // ESP32 TX  ->  GPS RX
constexpr uint32_t kGpsBaud     = 9600;

const char *labelForI2cAddr(uint8_t addr) {
  switch (addr) {
    case 0x20: return "PCA9535PW  IO expander";
    case 0x51: return "PCF85063   RTC";
    case 0x55: return "BQ27220    fuel gauge";
    case 0x5D: return "GT911      touch";
    case 0x68: return "TPS65185   EPD PMIC";
    case 0x6B: return "BQ25896    charger";
    default:   return "?";
  }
}

void scanI2c() {
  Wire.end();
  Wire.begin(kI2cSda, kI2cScl, kI2cFreq);
  // Bound any hung transactions so a stuck device can't lock the scan.
  Wire.setTimeOut(50);

  Serial.printf("[i2c] scanning bus (SDA=%u, SCL=%u, %u Hz)\n",
                kI2cSda, kI2cScl, kI2cFreq);
  uint8_t found = 0;
  for (uint8_t addr = 1; addr < 127; ++addr) {
    Wire.beginTransmission(addr);
    uint8_t err = Wire.endTransmission();
    if (err == 0) {
      Serial.printf("       0x%02X  %s\n", addr, labelForI2cAddr(addr));
      ++found;
    }
  }
  Serial.printf("[i2c] %u device%s found\n", found, found == 1 ? "" : "s");
}

void sniffGps(uint32_t window_ms) {
  Serial2.begin(kGpsBaud, SERIAL_8N1, kGpsRxFromGps, kGpsTxToGps);
  Serial.printf("[gps] listening on UART2 (RX=%u, TX=%u) @ %u baud for %u ms\n",
                kGpsRxFromGps, kGpsTxToGps, kGpsBaud, window_ms);

  uint32_t start = millis();
  uint32_t bytes = 0;
  uint32_t lines = 0;
  String line;
  while (millis() - start < window_ms) {
    while (Serial2.available()) {
      char c = static_cast<char>(Serial2.read());
      ++bytes;
      if (c == '\n') {
        if (line.startsWith("$")) {
          Serial.printf("       %s\n", line.c_str());
          ++lines;
        }
        line = "";
      } else if (c != '\r') {
        line += c;
      }
    }
    delay(5);
  }

  Serial.printf("[gps] received %u bytes, %u NMEA sentences in %u ms\n",
                bytes, lines, window_ms);
  if (bytes == 0) {
    Serial.println("[gps] no data — module may be unpowered (PCA9535 EN line) "
                   "or antenna disconnected");
  } else if (lines == 0) {
    Serial.println("[gps] data but no NMEA framing — wrong baud or noise");
  }
}

}  // namespace

void setup() {
  Serial.begin(115200);
  // Native USB-CDC re-enumerates after RTS hard-reset; give the host plenty
  // of time to reattach so the boot banner isn't lost.
  for (uint32_t start = millis(); !Serial && millis() - start < 5000;) {
    delay(50);
  }
  for (int i = 0; i < 5; ++i) {
    Serial.println(".... waiting for serial monitor ....");
    delay(200);
  }

  Serial.println();
  Serial.println("=== LilyGo T5 E-Paper S3 Pro (H752-02, 915 MHz + GPS) ===");
  Serial.printf("Chip:    %s rev %d, %d core(s) @ %u MHz\n",
                ESP.getChipModel(), ESP.getChipRevision(),
                ESP.getChipCores(), ESP.getCpuFreqMHz());
  Serial.printf("Flash:   %u MB\n", ESP.getFlashChipSize() / (1024U * 1024U));
  Serial.printf("PSRAM:   %u KB (free %u KB)\n",
                ESP.getPsramSize() / 1024U, ESP.getFreePsram() / 1024U);
  Serial.printf("Heap:    %u KB free\n", ESP.getFreeHeap() / 1024U);

  scanI2c();
  sniffGps(5000);

  Serial.println("[ok] sanity check complete — entering heartbeat loop.");
}

void loop() {
  static uint32_t tick = 0;
  Serial.printf("[heartbeat] %lu  (free heap=%u KB)\n",
                static_cast<unsigned long>(++tick),
                ESP.getFreeHeap() / 1024U);
  delay(1000);
}
