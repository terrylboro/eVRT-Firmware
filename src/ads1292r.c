#include "ads1292r.h"
#include <errno.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/spi.h>
#include <hal/nrf_common.h>

#define ADS_NODE DT_NODELABEL(ads1292r)
#define CMD_START 0x08
#define CMD_STOP 0x0a
#define CMD_SDATAC 0x11
#define CMD_RREG 0x20
#define CMD_WREG 0x40
#define ADS_SPI_REG ((void *)DT_REG_ADDR(DT_BUS(ADS_NODE)))

#define REG_CONFIG1 0x01
#define REG_CONFIG2 0x02
#define REG_LOFF 0x03
#define REG_CH1SET 0x04
#define REG_CH2SET 0x05
#define REG_RLDSENS 0x06
#define REG_LOFFSENS 0x07
#define REG_RESP1 0x09
#define REG_RESP2 0x0a

/* Mode 1 comes from spi-cpha in DT. 10 us CS setup/hold exceeds
 * the 3 tCLK final-clock-to-CS minimum at nominal 512 kHz CLK.
 * SPIM00 on nRF54L15 uses a 128 MHz source with an even prescaler. 1.28 MHz maps exactly to divisor 100 and stays comfortably below the ADS1292R limit.
 */
static const struct spi_dt_spec bus =
    SPI_DT_SPEC_GET(ADS_NODE, SPI_OP_MODE_MASTER | SPI_WORD_SET(8), 10);
static const struct gpio_dt_spec cs = GPIO_DT_SPEC_GET(ADS_NODE, cs_gpios);
static const struct gpio_dt_spec start = GPIO_DT_SPEC_GET(ADS_NODE, start_gpios);
static const struct gpio_dt_spec reset = GPIO_DT_SPEC_GET(ADS_NODE, reset_gpios);
static const struct gpio_dt_spec drdy = GPIO_DT_SPEC_GET(ADS_NODE, drdy_gpios);
static K_MUTEX_DEFINE(lock);
static bool ready;

// static uint8_t tx_cmd;
// static uint8_t rx_ignored;

static int transfer(uint8_t *tx, uint8_t *rx, size_t length);

/* Caller holds lock. Buffers are in RAM for EasyDMA. CS is controlled manually
 * so register sequences can keep it low across command bytes while still
 * inserting ADS1292R decode-time gaps.
 */
static int transfer(uint8_t *tx, uint8_t *rx, size_t length)
{
    struct spi_buf tx_buf = {.buf = tx, .len = length};
    struct spi_buf rx_buf = {.buf = rx, .len = length};
    struct spi_config config = bus.config;
    const struct spi_buf_set tx_set = {.buffers = &tx_buf, .count = 1};
    const struct spi_buf_set rx_set = {
        .buffers = rx ? &rx_buf : NULL,
        .count = rx ? 1 : 0,
    };

    config.cs.gpio.port = NULL;
    printk("SPI transfer: tx=%p dma=%d rx=%p dma=%d len=%u\n",
           tx, nrf_dma_accessible_check(ADS_SPI_REG, tx),
           rx, rx ? nrf_dma_accessible_check(ADS_SPI_REG, rx) : 1,
           (unsigned int)length);
    int ret = spi_transceive(bus.bus, &config, &tx_set, rx ? &rx_set : NULL);
    k_busy_wait(10);
    return ret;
}

static int send_command(uint8_t value)
{
    int ret = gpio_pin_set_dt(&cs, 1);

    if (ret) { return ret; }
    k_busy_wait(2000);
    ret = gpio_pin_set_dt(&cs, 0);
    if (ret) { return ret; }
    k_busy_wait(2000);
    ret = gpio_pin_set_dt(&cs, 1);

    if (ret) { return ret; }
    k_busy_wait(2000);
    ret = transfer(&value, NULL, 1);
    k_busy_wait(2000);
    int cs_ret = gpio_pin_set_dt(&cs, 0);
    return ret ? ret : cs_ret;
}

static uint8_t sanitize_write(uint8_t address, uint8_t value)
{
    switch (address) {
    case REG_CONFIG1:
        return value & 0x87;
    case REG_CONFIG2:
        return (value & 0xfb) | 0x80;
    case REG_LOFF:
        return (value & 0xfd) | 0x10;
    case REG_LOFFSENS:
        return value & 0x3f;
    case 0x08:
        return value & 0x5f;
    case REG_RESP1:
        return value | 0x02;
    case REG_RESP2:
        return (value & 0x87) | 0x01;
    case 0x0b:
        return value & 0x0f;
    default:
        return value;
    }
}

static int register_transaction_locked(uint8_t address, uint8_t *read_values,
                                       const uint8_t *write_values, size_t count)
{
    uint8_t command = (write_values ? CMD_WREG : CMD_RREG) | address;
    uint8_t byte_count = count - 1;
    uint8_t zeros[ADS1292R_REGISTER_COUNT] = {0};
    uint8_t rx[ADS1292R_REGISTER_COUNT] = {0};
    int ret;
    int cs_ret;

    ret = gpio_pin_set_dt(&cs, 1);
    if (ret) { return ret; }
    k_busy_wait(10);

    ret = transfer(&command, NULL, 1);
    if (ret) { goto out; }
    ret = transfer(&byte_count, NULL, 1);
    if (ret) { goto out; }

    if (write_values) {
        memcpy(zeros, write_values, count);
        ret = transfer(zeros, NULL, count);
    } else {
        ret = transfer(zeros, rx, count);
        if (ret == 0) {
            printk("ADS1292R RX:");
            for (size_t i = 0; i < count; ++i) {
                printk(" %02x", rx[i]);
            }
            printk("\n");
            memcpy(read_values, rx, count);
        }
    }

out:
    cs_ret = gpio_pin_set_dt(&cs, 0);
    if (ret == 0) { ret = cs_ret; }
    return ret;
}

int ads1292r_spi_smoke_test(void)
{
    int ret;
    uint8_t pattern = 0xaa;

    printk("ADS1292R SPI smoke test: CS low, sending 0xaa eight times\n");

    if (!device_is_ready(bus.bus) || !gpio_is_ready_dt(&cs)) {
        printk("ADS1292R SPI smoke test: spi=%d cs=%d\n",
               device_is_ready(bus.bus), gpio_is_ready_dt(&cs));
        return -ENODEV;
    }

    ret = gpio_pin_configure_dt(&cs, GPIO_OUTPUT_INACTIVE);
    if (ret) { return ret; }

    for (int i = 0; i < 8; ++i) {
        ret = gpio_pin_set_dt(&cs, 1);
        if (ret) { return ret; }
        k_busy_wait(10);
        ret = transfer(&pattern, NULL, 1);
        k_busy_wait(10);
        int cs_ret = gpio_pin_set_dt(&cs, 0);
        if (ret || cs_ret) { return ret ? ret : cs_ret; }
        k_sleep(K_MSEC(100));
    }

    return 0;
}
int ads1292r_init(void)
{
    int ret;

    k_mutex_lock(&lock, K_FOREVER);
    ready = false;
    printk("ADS init step 1: checking devices\n");
    printk("ADS init ready: spi=%d cs=%d start=%d reset=%d drdy=%d\n",
           device_is_ready(bus.bus), gpio_is_ready_dt(&cs),
           gpio_is_ready_dt(&start), gpio_is_ready_dt(&reset), gpio_is_ready_dt(&drdy));
    if (!device_is_ready(bus.bus) || !gpio_is_ready_dt(&start) ||
        !gpio_is_ready_dt(&reset) || !gpio_is_ready_dt(&drdy) ||
        !gpio_is_ready_dt(&cs)) {
        ret = -ENODEV;
        goto out;
    }
    ret = gpio_pin_configure_dt(&cs, GPIO_OUTPUT_INACTIVE);
    if (ret) { goto out; }
    ret = gpio_pin_configure_dt(&start, GPIO_OUTPUT_ACTIVE);
    if (ret) { goto out; }
    ret = gpio_pin_configure_dt(&reset, GPIO_OUTPUT_INACTIVE);
    if (ret) { goto out; }
    ret = gpio_pin_configure_dt(&drdy, GPIO_INPUT);
    if (ret) { goto out; }
    printk("ADS init step 2: GPIO configured\n");
    printk("Running ADS1292R startup sequence without GPIO reset pulse\n");

    /* Conservative POR/oscillator startup delay. Keep START low and send only SDATAC before ID reads so hardware bring-up is not obscured by register writes or conversion-control commands. */
    k_sleep(K_SECONDS(1));

    ret = gpio_pin_set_dt(&reset, 0);
    if (ret) { goto out; }
    printk("ADS init step 3: reset held inactive, wait complete\n");

    printk("Holding START low and sending SDATAC only\n");
    ret = gpio_pin_set_dt(&start, 0);
    if (ret) { goto out; }
    k_sleep(K_MSEC(100));

    ret = send_command(CMD_SDATAC);
    if (ret) { goto out; }
    printk("Sent SDATAC command to ADS1292R, ret=%d\n", ret);
    k_sleep(K_MSEC(300));
    printk("ADS init: ID-only mode, skipping register writes\n");
    ready = true;
    ret = 0;
out:
    k_mutex_unlock(&lock);
    return ret;
}

static int registers(uint8_t address, uint8_t *read_values,
                     const uint8_t *write_values, size_t count)
{
    int ret;

    if (count == 0 || address >= ADS1292R_REGISTER_COUNT ||
        count > ADS1292R_REGISTER_COUNT - address) {
        return -EINVAL;
    }
    k_mutex_lock(&lock, K_FOREVER);
    if (!ready) {
        ret = -EACCES;
        goto out;
    }

    ret = register_transaction_locked(address, read_values, write_values, count);

out:
    k_mutex_unlock(&lock);
    return ret;
}

int ads1292r_read_registers(uint8_t address, uint8_t *values, size_t count)
{
    if (!values) { return -EINVAL; }
    return registers(address, values, NULL, count);
}

int ads1292r_write_registers(uint8_t address, const uint8_t *values, size_t count)
{
    if (!values || address == ADS1292R_REG_ID) { return -EINVAL; }
    return registers(address, NULL, values, count);
}









