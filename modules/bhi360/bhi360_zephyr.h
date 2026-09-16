#ifndef BHI360_ZEPHYR_H
#define BHI360_ZEPHYR_H

#include <stdbool.h>
#include <stdint.h>

struct bhi360_zephyr_info {
    uint8_t chip_id;
    uint8_t product_id;
    uint8_t revision_id;
    uint16_t rom_version;
    uint16_t kernel_version;
    uint8_t boot_status;
    uint8_t sensor_error;
};

struct bhi360_imu_sample {
    float ax_g;
    float ay_g;
    float az_g;
    float gx_dps;
    float gy_dps;
    float gz_dps;
    int64_t timestamp_ms;
    bool has_accel;
    bool has_gyro;
};

int bhi360_zephyr_boot(struct bhi360_zephyr_info *info);
int bhi360_zephyr_start_streaming(float sample_rate_hz);
int bhi360_zephyr_process_fifo(void);
int bhi360_zephyr_get_latest(struct bhi360_imu_sample *sample);

#endif
