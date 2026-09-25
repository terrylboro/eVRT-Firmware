#include <stdint.h>

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include "bno055.h"

static void print_quat(const struct bno055_quat *quat,
                       const struct bno055_calibration *cal)
{
    /*
     * BNO055 quaternion scale is 1 / 2^14. Print raw values and milli-units
     * to avoid pulling floating point formatting into the example.
     */
    int32_t w_milli = ((int32_t)quat->w * 1000) / 16384;
    int32_t x_milli = ((int32_t)quat->x * 1000) / 16384;
    int32_t y_milli = ((int32_t)quat->y * 1000) / 16384;
    int32_t z_milli = ((int32_t)quat->z * 1000) / 16384;

    printk("quat raw w=%d x=%d y=%d z=%d | milli w=%ld x=%ld y=%ld z=%ld | cal s/g/a/m=%u/%u/%u/%u\n",
           quat->w, quat->x, quat->y, quat->z,
           (long)w_milli, (long)x_milli, (long)y_milli, (long)z_milli,
           cal->system, cal->gyro, cal->accel, cal->mag);
}

int main(void)
{
    uint8_t chip_id = 0;
    int ret;

    printk("BNO055 I2C probe: SCL P0.00, SDA P0.01\n");

    ret = bno055_read_chip_id(&chip_id);
    printk("BNO055 early chip ID read: ret=%d id=0x%02x\n", ret, chip_id);

    ret = bno055_init();
    if (ret) {
        printk("BNO055 init failed: %d\n", ret);
        return 0;
    }

    ret = bno055_read_chip_id(&chip_id);
    printk("BNO055 ready: chip ID=0x%02x ret=%d\n", chip_id, ret);
    printk("BNO055 NDOF quaternion polling\n");

    while (1) {
        struct bno055_quat quat;
        struct bno055_calibration cal = { 0 };

        ret = bno055_read_quat(&quat);
        if (ret) {
            printk("BNO055 quaternion read failed: %d\n", ret);
            k_sleep(K_MSEC(500));
            continue;
        }

        ret = bno055_read_calibration(&cal);
        if (ret) {
            printk("BNO055 calibration read failed: %d\n", ret);
        }

        print_quat(&quat, &cal);
        k_sleep(K_MSEC(200));
    }
}
