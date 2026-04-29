# Shared ESP32-S3 helpers

- `bq27220_idf_read.h` — BQ27220 reads over `i2c_master_*` when epdiy holds `I2C_NUM_0` (Wire is unusable after `epd_init` on T5 Pro).
