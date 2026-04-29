// Read BQ27220 via ESP-IDF I2C while epdiy owns I2C_NUM_0.  Arduino Wire
// returns 0xFF on that bus after epd_board_v7, which decoded as ~65 V and 65535%.

#pragma once

#include <driver/i2c.h>
#include <stdint.h>

#include "bq27220_def.h"

// Voltage (mV), SoC (%), charging flag matching BQ27220::getIsCharging (!DSG).
inline bool bq27220_idf_read_live(i2c_port_t port, uint16_t *mv_millivolt,
                                  uint16_t *soc_pct, bool *is_charging) {
    auto rd16 = [port](uint8_t reg, uint16_t *o) -> bool {
        uint8_t data[2];
        esp_err_t err = i2c_master_write_read_device(
            port, BQ27220_I2C_ADDRESS, &reg, 1, data, 2, pdMS_TO_TICKS(50));
        if (err != ESP_OK) return false;
        *o = (uint16_t)data[0] | ((uint16_t)data[1] << 8);
        return true;
    };
    uint16_t v, s, stat;
    if (!rd16(CommandVoltage, &v) || !rd16(CommandStateOfCharge, &s) ||
        !rd16(CommandBatteryStatus, &stat))
        return false;
    if (v == 0xFFFFu || v < 2600 || v > 4350) return false;
    if (s > 100u) return false;
    *mv_millivolt = v;
    *soc_pct      = s;
    *is_charging  = ((stat & 1u) == 0u);
    return true;
}
