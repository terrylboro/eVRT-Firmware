#include <zephyr/device.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>

#define BHI_I2C_NODE DT_ALIAS(bhi385_i2c)

static const uint8_t expected_addrs[] = { 0x28, 0x29 };

static bool probe_i2c_addr(const struct device *bus, uint8_t addr)
{
    struct i2c_msg msg = {
        .buf = NULL,
        .len = 0,
        .flags = I2C_MSG_WRITE | I2C_MSG_STOP,
    };

    return i2c_transfer(bus, &msg, 1, addr) == 0;
}

int main(void)
{
    const struct device *i2c = DEVICE_DT_GET(BHI_I2C_NODE);
    bool found_expected = false;

    printk("BHI385/BHI360 shuttle I2C scan: P1.12 SDA, P1.11 SCL\n");

    if (!device_is_ready(i2c)) {
        printk("I2C bus %s is not ready\n", i2c->name);
        return 0;
    }

    printk("I2C bus ready: %s\n", i2c->name);
    printk("Checking expected BHI addresses...\n");

    for (size_t i = 0; i < ARRAY_SIZE(expected_addrs); i++) {
        uint8_t addr = expected_addrs[i];
        bool present = probe_i2c_addr(i2c, addr);

        printk("  0x%02x: %s\n", addr, present ? "ACK" : "no response");
        found_expected |= present;
    }

    printk("Scanning 0x20..0x2f for nearby devices...\n");
    for (uint8_t addr = 0x20; addr <= 0x2f; addr++) {
        if (probe_i2c_addr(i2c, addr)) {
            printk("  found device at 0x%02x\n", addr);
        }
    }

    if (found_expected) {
        printk("BHI shuttle detected. Next step: Bosch SensorAPI firmware upload.\n");
    } else {
        printk("No BHI ACK at 0x28/0x29. Check power, GND, SDA/SCL order, and address strap.\n");
    }

    return 0;
}
