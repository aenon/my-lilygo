// GT911 recovery — send normal mode command (0x00) to start touch scanning.

#include <Wire.h>
#include <Arduino.h>

#define SENSOR_SDA  39
#define SENSOR_SCL  40
#define GT911_ADDR  0x5D

void gt911_write(uint16_t reg, uint8_t val) {
    Wire.beginTransmission(GT911_ADDR);
    Wire.write(reg >> 8); Wire.write(reg & 0xFF); Wire.write(val);
    Wire.endTransmission();
}

uint8_t gt911_read(uint16_t reg) {
    Wire.beginTransmission(GT911_ADDR);
    Wire.write(reg >> 8); Wire.write(reg & 0xFF);
    Wire.endTransmission(false);
    Wire.requestFrom(GT911_ADDR, (uint8_t)1);
    return Wire.available() ? Wire.read() : 0xFF;
}

void gt911_read_multi(uint16_t reg, uint8_t *buf, size_t len) {
    Wire.beginTransmission(GT911_ADDR);
    Wire.write(reg >> 8); Wire.write(reg & 0xFF);
    Wire.endTransmission(false);
    Wire.requestFrom(GT911_ADDR, (uint8_t)len);
    for (size_t i = 0; i < len && Wire.available(); i++) buf[i] = Wire.read();
}

void setup()
{
    Serial.begin(115200);
    delay(500);
    Wire.begin(SENSOR_SDA, SENSOR_SCL);

    Serial.println("GT911: send normal mode command");

    // Software reset first
    gt911_write(0x8040, 0x02);
    delay(200);
    Serial.printf("After reset, cmd=0x%02X\n", gt911_read(0x8040));

    // Send normal mode command (0x00) to start scanning
    gt911_write(0x8040, 0x00);
    delay(100);
    Serial.printf("After normal mode, cmd=0x%02X\n", gt911_read(0x8040));

    // Read resolution and product ID
    uint8_t xlo = gt911_read(0x8146), xhi = gt911_read(0x8147);
    uint8_t ylo = gt911_read(0x8148), yhi = gt911_read(0x8149);
    Serial.printf("Resolution: %dx%d\n", (xhi<<8)|xlo, (yhi<<8)|ylo);
    Serial.printf("Product ID: %c%c%c%c\n",
                  gt911_read(0x8140), gt911_read(0x8141),
                  gt911_read(0x8142), gt911_read(0x8143));

    // Clear the POINT_INFO buffer
    gt911_write(0x814E, 0x00);
    delay(10);
    Serial.printf("POINT_INFO after clear: 0x%02X\n", gt911_read(0x814E));

    Serial.println("Tap the screen:");
}

void loop()
{
    uint8_t info = gt911_read(0x814E);
    uint8_t n = info & 0x0F;

    if (n > 0) {
        uint8_t pt[8];
        gt911_read_multi(0x814F, pt, 8);
        Serial.printf("TOUCH! X=%d Y=%d\n", pt[1]|(pt[2]<<8), pt[3]|(pt[4]<<8));
        gt911_write(0x814E, 0x00);  // clear buffer
    }

    static uint32_t lastLog = 0;
    if (millis() - lastLog > 2000) {
        lastLog = millis();
        Serial.printf("[poll] POINT_INFO=0x%02X n=%d\n", info, n);
    }

    delay(50);
}
