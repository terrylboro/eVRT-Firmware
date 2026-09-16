#include <stdint.h>

#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>

#define I2C_GPIO_NODE DT_NODELABEL(gpio1)
#define I2C_SCL_PIN 11
#define I2C_SDA_PIN 12
#define BHI360_REG_CHIP_ID 0x2b

static const struct device *gpio = DEVICE_DT_GET(I2C_GPIO_NODE);

static void i2c_delay(void)
{
    k_busy_wait(50);
}

static void line_low(gpio_pin_t pin)
{
    (void)gpio_pin_configure(gpio, pin, GPIO_OUTPUT_LOW);
}

static void line_release(gpio_pin_t pin)
{
    (void)gpio_pin_configure(gpio, pin, GPIO_INPUT | GPIO_PULL_UP);
}

static int line_read(gpio_pin_t pin)
{
    return gpio_pin_get(gpio, pin);
}

static void scl_low(void)
{
    line_low(I2C_SCL_PIN);
}

static void scl_release(void)
{
    line_release(I2C_SCL_PIN);
}

static bool scl_wait_high(void)
{
    for (int i = 0; i < 100; i++) {
        if (line_read(I2C_SCL_PIN)) {
            return true;
        }

        k_busy_wait(10);
    }

    return false;
}

static void sda_low(void)
{
    line_low(I2C_SDA_PIN);
}

static void sda_release(void)
{
    line_release(I2C_SDA_PIN);
}

static void i2c_start(void)
{
    sda_release();
    scl_release();
    i2c_delay();
    sda_low();
    i2c_delay();
    scl_low();
    i2c_delay();
}

static void i2c_stop(void)
{
    sda_low();
    i2c_delay();
    scl_release();
    i2c_delay();
    sda_release();
    i2c_delay();
}

static bool i2c_write_byte(uint8_t byte)
{
    for (int bit = 7; bit >= 0; bit--) {
        if (byte & BIT(bit)) {
            sda_release();
        } else {
            sda_low();
        }

        i2c_delay();
        scl_release();
        (void)scl_wait_high();
        i2c_delay();
        scl_low();
        i2c_delay();
    }

    sda_release();
    i2c_delay();
    scl_release();
    (void)scl_wait_high();
    i2c_delay();
    bool ack = line_read(I2C_SDA_PIN) == 0;
    scl_low();
    i2c_delay();

    return ack;
}

static uint8_t i2c_read_byte(bool ack)
{
    uint8_t byte = 0U;

    sda_release();
    for (int bit = 7; bit >= 0; bit--) {
        i2c_delay();
        scl_release();
        (void)scl_wait_high();
        i2c_delay();
        if (line_read(I2C_SDA_PIN)) {
            byte |= BIT(bit);
        }
        scl_low();
        i2c_delay();
    }

    if (ack) {
        sda_low();
    } else {
        sda_release();
    }

    i2c_delay();
    scl_release();
    (void)scl_wait_high();
    i2c_delay();
    scl_low();
    sda_release();
    i2c_delay();

    return byte;
}

static bool bitbang_probe(uint8_t addr)
{
    bool ack;

    i2c_start();
    ack = i2c_write_byte((uint8_t)(addr << 1));
    i2c_stop();

    return ack;
}

static int bitbang_read_reg(uint8_t addr, uint8_t reg, uint8_t *value)
{
    bool ack_addr_w;
    bool ack_reg;
    bool ack_addr_r;

    if (!value) {
        return -1;
    }

    *value = 0U;

    i2c_start();
    ack_addr_w = i2c_write_byte((uint8_t)(addr << 1));
    ack_reg = i2c_write_byte(reg);
    i2c_start();
    ack_addr_r = i2c_write_byte((uint8_t)((addr << 1) | 1U));

    if (ack_addr_w && ack_reg && ack_addr_r) {
        *value = i2c_read_byte(false);
    }

    i2c_stop();

    printk("  reg read 0x%02x/0x%02x: addr_w=%s reg=%s addr_r=%s value=0x%02x\n",
           addr, reg,
           ack_addr_w ? "ACK" : "NAK",
           ack_reg ? "ACK" : "NAK",
           ack_addr_r ? "ACK" : "NAK",
           *value);

    return (ack_addr_w && ack_reg && ack_addr_r) ? 0 : -1;
}

int main(void)
{
    static const uint8_t shuttle_addrs[] = { 0x14, 0x28, 0x29, 0x46, 0x77 };
    uint8_t chip_id;

    printk("BHI385/BHI360 GPIO bitbang I2C diagnostic: P1.12 SDA, P1.11 SCL\n");

    if (!device_is_ready(gpio)) {
        printk("gpio1 is not ready\n");
        return 0;
    }

    sda_release();
    scl_release();
    k_sleep(K_MSEC(20));

    printk("Idle levels: SCL=%d SDA=%d\n", line_read(I2C_SCL_PIN), line_read(I2C_SDA_PIN));

    printk("Known shuttle address probe:\n");
    for (size_t i = 0; i < ARRAY_SIZE(shuttle_addrs); i++) {
        bool ack = bitbang_probe(shuttle_addrs[i]);

        printk("  0x%02x: %s\n", shuttle_addrs[i], ack ? "ACK" : "no response");
        k_sleep(K_MSEC(2));
    }

    printk("Full 7-bit address scan:\n");
    for (uint8_t addr = 0x08; addr <= 0x77; addr++) {
        if (bitbang_probe(addr)) {
            printk("  found ACK at 0x%02x\n", addr);
        }
        k_sleep(K_MSEC(1));
    }

    printk("BHI360 chip-id reads:\n");
    (void)bitbang_read_reg(0x28, BHI360_REG_CHIP_ID, &chip_id);
    (void)bitbang_read_reg(0x29, BHI360_REG_CHIP_ID, &chip_id);

    printk("Bitbang diagnostic complete\n");

    return 0;
}
