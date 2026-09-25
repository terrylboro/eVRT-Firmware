#ifndef BNO055_H
#define BNO055_H

#include <stdint.h>

struct bno055_quat {
    int16_t w;
    int16_t x;
    int16_t y;
    int16_t z;
};

struct bno055_calibration {
    uint8_t system;
    uint8_t gyro;
    uint8_t accel;
    uint8_t mag;
};

int bno055_init(void);
int bno055_read_chip_id(uint8_t *chip_id);
int bno055_set_ndof_mode(void);
int bno055_read_quat(struct bno055_quat *quat);
int bno055_read_calibration(struct bno055_calibration *cal);

#endif
