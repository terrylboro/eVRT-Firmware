#include <zephyr/kernel.h>
#include "ads1292r.h"

int main(void)
{
    printk("ADS1292R firmware build: spi00-id-read v1\n");
    printk("ADS1292R SPI transport: mode 1, 1.28 MHz\n");

    int ret = ads1292r_init();
    if (ret) {
        printk("ADS1292R initialization failed: %d\n", ret);
        return 0;
    }

    while (1) {
        uint8_t id = 0;

        ret = ads1292r_read_registers(ADS1292R_REG_ID, &id, 1);
        if (ret) {
            printk("ADS1292R ID read failed: %d\n", ret);
        } else if (id == ADS1292R_EXPECTED_ID) {
            printk("ADS1292R ID OK: 0x%02x\n", id);
        } else {
            printk("ADS1292R ID mismatch: 0x%02x, expected 0x%02x\n",
                   id, ADS1292R_EXPECTED_ID);
        }

        k_sleep(K_SECONDS(1));
    }
}
