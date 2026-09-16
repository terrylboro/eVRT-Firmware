#include <zephyr/kernel.h>

#include "bhi360_zephyr.h"

static void print_fixed_3(const char *label, float value)
{
    int scaled = (int)(value * 1000.0f);
    int whole;
    int frac;

    printk("%s", label);

    if (scaled < 0) {
        printk("-");
        scaled = -scaled;
    }

    whole = scaled / 1000;
    frac = scaled % 1000;
    printk("%d.%03d", whole, frac);
}

int main(void)
{
    struct bhi360_zephyr_info info;
    struct bhi360_imu_sample sample;
    int ret;

    printk("BHI360 + BMM350 firmware boot baseline\n");

    ret = bhi360_zephyr_boot(&info);
    if (ret) {
        printk("BHI360 boot baseline failed: %d\n", ret);
        return ret;
    }

    printk("BHI360 baseline passed: chip=0x%02x product=0x%02x kernel=%u\n",
           info.chip_id, info.product_id, info.kernel_version);

    ret = bhi360_zephyr_start_streaming(50.0f);
    if (ret) {
        printk("BHI360 streaming start failed: %d\n", ret);
        return ret;
    }

    for (int i = 0; i < 200; i++) {
        ret = bhi360_zephyr_process_fifo();
        if (ret) {
            printk("BHI360 streaming failed: %d\n", ret);
            return ret;
        }

        if ((i % 13) == 0 && bhi360_zephyr_get_latest(&sample) == 0) {
            printk("IMU t=%lldms acc_g=(", sample.timestamp_ms);
            print_fixed_3("", sample.ax_g);
            print_fixed_3(",", sample.ay_g);
            print_fixed_3(",", sample.az_g);
            printk(") gyro_dps=(");
            print_fixed_3("", sample.gx_dps);
            print_fixed_3(",", sample.gy_dps);
            print_fixed_3(",", sample.gz_dps);
            printk(")\n");
        }

        k_sleep(K_MSEC(20));
    }

    printk("BHI360 streaming baseline complete\n");

    return 0;
}
