#include "bno055.h"

#include <errno.h>
#include <stdint.h>

#include <zephyr/device.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#define BNO055_NODE DT_NODELABEL(bno055)

#if !DT_NODE_EXISTS(BNO055_NODE)
#error "BNO055 devicetree node label 'bno055' is required"
#endif

#define BNO055_CHIP_ID 0xA0

#define BNO055_REG_CHIP_ID 0x00
#define BNO055_REG_PAGE_ID 0x07
#define BNO055_REG_QUATERNION_DATA_W_LSB 0x20
#define BNO055_REG_CALIB_STAT 0x35
#define BNO055_REG_UNIT_SEL 0x3B
#define BNO055_REG_OPR_MODE 0x3D
#define BNO055_REG_PWR_MODE 0x3E
#define BNO055_REG_SYS_TRIGGER 0x3F

#define BNO055_OPR_MODE_CONFIG 0x00
#define BNO055_OPR_MODE_NDOF 0x0C
#define BNO055_PWR_MODE_NORMAL 0x00

static const struct i2c_dt_spec bno055_i2c = I2C_DT_SPEC_GET(BNO055_NODE);

static int bno055_reg_read(uint8_t reg, uint8_t *value)
{
    return i2c_reg_read_byte_dt(&bno055_i2c, reg, value);
}

static int bno055_reg_write(uint8_t reg, uint8_t value)
{
    return i2c_reg_write_byte_dt(&bno055_i2c, reg, value);
}

static int bno055_burst_read(uint8_t reg, uint8_t *buf, size_t len)
{
    return i2c_burst_read_dt(&bno055_i2c, reg, buf, len);
}

static int bno055_set_mode(uint8_t mode)
{
    int ret = bno055_reg_write(BNO055_REG_OPR_MODE, mode);

    if (ret) {
        return ret;
    }

    k_sleep(mode == BNO055_OPR_MODE_CONFIG ? K_MSEC(25) : K_MSEC(35));
    return 0;
}

int bno055_read_chip_id(uint8_t *chip_id)
{
    if (!chip_id) {
        return -EINVAL;
    }

    return bno055_reg_read(BNO055_REG_CHIP_ID, chip_id);
}

int bno055_init(void)
{
    uint8_t chip_id = 0;
    int ret;

    if (!device_is_ready(bno055_i2c.bus)) {
        printk("BNO055 I2C bus is not ready\n");
        return -ENODEV;
    }

    /*
     * After power-up or soft reset the BNO055 can take a little while before
     * CHIP_ID reads as 0xA0.
     */
    for (int attempt = 0; attempt < 20; attempt++) {
        ret = bno055_read_chip_id(&chip_id);
        if (ret == 0 && chip_id == BNO055_CHIP_ID) {
            break;
        }
        k_sleep(K_MSEC(50));
    }

    if (chip_id != BNO055_CHIP_ID) {
        printk("BNO055 chip ID mismatch: 0x%02x\n", chip_id);
        return -ENODEV;
    }

    ret = bno055_set_mode(BNO055_OPR_MODE_CONFIG);
    if (ret) {
        return ret;
    }

    ret = bno055_reg_write(BNO055_REG_PAGE_ID, 0x00);
    if (ret) {
        return ret;
    }

    ret = bno055_reg_write(BNO055_REG_PWR_MODE, BNO055_PWR_MODE_NORMAL);
    if (ret) {
        return ret;
    }
    k_sleep(K_MSEC(10));

    ret = bno055_reg_write(BNO055_REG_SYS_TRIGGER, 0x00);
    if (ret) {
        return ret;
    }
    k_sleep(K_MSEC(10));

    /* Android orientation, Celsius, degrees, dps, m/s^2: reset defaults. */
    ret = bno055_reg_write(BNO055_REG_UNIT_SEL, 0x00);
    if (ret) {
        return ret;
    }

    return bno055_set_ndof_mode();
}

int bno055_set_ndof_mode(void)
{
    int ret = bno055_set_mode(BNO055_OPR_MODE_CONFIG);

    if (ret) {
        return ret;
    }

    return bno055_set_mode(BNO055_OPR_MODE_NDOF);
}

int bno055_read_quat(struct bno055_quat *quat)
{
    uint8_t raw[8];
    int ret;

    if (!quat) {
        return -EINVAL;
    }

    ret = bno055_burst_read(BNO055_REG_QUATERNION_DATA_W_LSB, raw, sizeof(raw));
    if (ret) {
        return ret;
    }

    quat->w = (int16_t)((uint16_t)raw[0] | ((uint16_t)raw[1] << 8));
    quat->x = (int16_t)((uint16_t)raw[2] | ((uint16_t)raw[3] << 8));
    quat->y = (int16_t)((uint16_t)raw[4] | ((uint16_t)raw[5] << 8));
    quat->z = (int16_t)((uint16_t)raw[6] | ((uint16_t)raw[7] << 8));

    return 0;
}

int bno055_read_calibration(struct bno055_calibration *cal)
{
    uint8_t stat;
    int ret;

    if (!cal) {
        return -EINVAL;
    }

    ret = bno055_reg_read(BNO055_REG_CALIB_STAT, &stat);
    if (ret) {
        return ret;
    }

    cal->system = (stat >> 6) & 0x03;
    cal->gyro = (stat >> 4) & 0x03;
    cal->accel = (stat >> 2) & 0x03;
    cal->mag = stat & 0x03;

    return 0;
}
