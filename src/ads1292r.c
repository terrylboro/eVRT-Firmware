#include "ads1292r.h"
#include <errno.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/spi.h>

#define ADS_NODE DT_NODELABEL(ads1292r)
#define CMD_START 0x08
#define CMD_STOP 0x0a
#define CMD_RDATAC 0x10
#define CMD_SDATAC 0x11
#define CMD_RDATA 0x12
#define CMD_RREG 0x20
#define CMD_WREG 0x40

#define REG_CONFIG1 ADS1292R_REG_CONFIG1
#define REG_CONFIG2 ADS1292R_REG_CONFIG2
#define REG_LOFF ADS1292R_REG_LOFF
#define REG_CH1SET ADS1292R_REG_CH1SET
#define REG_CH2SET ADS1292R_REG_CH2SET
#define REG_RLDSENS ADS1292R_REG_RLDSENS
#define REG_LOFFSENS ADS1292R_REG_LOFFSENS
#define REG_RESP1 ADS1292R_REG_RESP1
#define REG_RESP2 ADS1292R_REG_RESP2
#define REG_GPIO ADS1292R_REG_GPIO

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
    int ret = spi_transceive(bus.bus, &config, &tx_set, rx ? &rx_set : NULL);
    k_busy_wait(10);
    return ret;
}

static int command_locked(uint8_t value)
{
    int ret = gpio_pin_set_dt(&cs, 1);
    if (ret) { return ret; }

    k_busy_wait(100);
    ret = transfer(&value, NULL, 1);
    k_busy_wait(100);

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
    k_sleep(K_MSEC(2));

    ret = transfer(&command, NULL, 1);
    if (ret) { goto out; }
    k_sleep(K_MSEC(2));
    ret = transfer(&byte_count, NULL, 1);
    if (ret) { goto out; }
    k_sleep(K_MSEC(2));

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

static int write_register_locked(uint8_t address, uint8_t value)
{
    uint8_t safe = sanitize_write(address, value);
    int ret = register_transaction_locked(address, NULL, &safe, 1);

    printk("WREG 0x%02x=0x%02x ret=%d\n", address, safe, ret);
    k_sleep(K_MSEC(10));
    return ret;
}

static int configure_internal_test_locked(void)
{
    int ret;

    /* Datasheet test-signal setup for bring-up without electrodes:
     * - CONFIG1 0x00: 125 SPS, high-resolution mode default fields.
     * - CONFIG2 0xa3: internal reference/test source enabled, 1 Hz test signal.
     * - CH1SET 0x45: channel 1 enabled, gain 6, input mux = test signal.
     * - CH2SET 0x45: channel 2 enabled, gain 6, input mux = test signal.
     * Other values follow the ProtoCentral baseline, with respiration disabled for now.
     */
    ret = write_register_locked(REG_CONFIG1, 0x00);
    if (ret) { return ret; }
    ret = write_register_locked(REG_CONFIG2, 0xa3);
    if (ret) { return ret; }
    ret = write_register_locked(REG_LOFF, 0x10);
    if (ret) { return ret; }
    ret = write_register_locked(REG_CH1SET, 0x45);
    if (ret) { return ret; }
    ret = write_register_locked(REG_CH2SET, 0x45);
    if (ret) { return ret; }
    ret = write_register_locked(REG_RLDSENS, 0x00);
    if (ret) { return ret; }
    ret = write_register_locked(REG_LOFFSENS, 0x00);
    if (ret) { return ret; }
    ret = write_register_locked(REG_RESP1, 0x02);
    if (ret) { return ret; }
    ret = write_register_locked(REG_RESP2, 0x01);
    if (ret) { return ret; }
    ret = write_register_locked(REG_GPIO, 0x0c);
    if (ret) { return ret; }

    return 0;
}

int ads1292r_init(void)
{
    int ret;
    uint8_t regs[ADS1292R_REGISTER_COUNT] = {0};

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
    ret = gpio_pin_configure_dt(&start, GPIO_OUTPUT_INACTIVE);
    if (ret) { goto out; }
    ret = gpio_pin_configure_dt(&reset, GPIO_OUTPUT_INACTIVE);
    if (ret) { goto out; }
    ret = gpio_pin_configure_dt(&drdy, GPIO_INPUT);
    if (ret) { goto out; }
    printk("ADS init step 2: GPIO configured\n");
    printk("Running ADS1292R initial power-up flow with internal test signals\n");

    ret = gpio_pin_set_dt(&cs, 0);
    if (ret) { goto out; }
    ret = gpio_pin_set_dt(&start, 0);
    if (ret) { goto out; }

    printk("ADS init step 3: /RESET low for 100 ms\n");
    ret = gpio_pin_set_dt(&reset, 1);
    if (ret) { goto out; }
    k_sleep(K_MSEC(100));

    printk("ADS init step 4: /RESET high, waiting 1 s\n");
    ret = gpio_pin_set_dt(&reset, 0);
    if (ret) { goto out; }
    k_sleep(K_SECONDS(1));

    printk("ADS init step 5: SDATAC before register programming\n");
    ret = command_locked(CMD_SDATAC);
    if (ret) { goto out; }
    k_sleep(K_MSEC(300));

    printk("ADS init step 6: programming internal test-signal registers\n");
    ret = configure_internal_test_locked();
    if (ret) { goto out; }

    ret = register_transaction_locked(ADS1292R_REG_ID, regs, NULL, ADS1292R_REGISTER_COUNT);
    if (ret) { goto out; }
    printk("ADS register image:");
    for (size_t i = 0; i < ADS1292R_REGISTER_COUNT; ++i) {
        printk(" %02x", regs[i]);
    }
    printk("\n");

    printk("ADS init step 7: START pin high, START command, and RDATAC\n");
    ret = gpio_pin_set_dt(&start, 1);
    if (ret) { goto out; }
    k_sleep(K_MSEC(10));
    ret = command_locked(CMD_START);
    if (ret) { goto out; }
    k_sleep(K_MSEC(10));
    ret = command_locked(CMD_RDATAC);
    if (ret) { goto out; }

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



int ads1292r_wait_for_sample(k_timeout_t timeout)
{
    k_timepoint_t end = sys_timepoint_calc(timeout);

    do {
        int value = gpio_pin_get_dt(&drdy);

        if (value < 0) {
            return value;
        }
        if (value > 0) {
            return 0;
        }
        k_sleep(K_MSEC(1));
    } while (!sys_timepoint_expired(end));

    return -ETIMEDOUT;
}

static int32_t sign_extend_24(const uint8_t bytes[3])
{
    int32_t value = ((int32_t)bytes[0] << 16) | ((int32_t)bytes[1] << 8) | bytes[2];

    if (value & 0x00800000) {
        value |= 0xff000000;
    }

    return value;
}

int ads1292r_read_sample(struct ads1292r_sample *sample)
{
    uint8_t zeros[ADS1292R_FRAME_SIZE] = {0};
    uint8_t rx[ADS1292R_FRAME_SIZE] = {0};
    int ret;
    int cs_ret;

    if (!sample) { return -EINVAL; }

    k_mutex_lock(&lock, K_FOREVER);
    if (!ready) {
        ret = -EACCES;
        goto out;
    }

    ret = gpio_pin_set_dt(&cs, 1);
    if (ret) { goto out; }
    k_busy_wait(100);

    ret = transfer(zeros, rx, sizeof(rx));
    k_busy_wait(100);

    cs_ret = gpio_pin_set_dt(&cs, 0);
    if (ret == 0) { ret = cs_ret; }
    if (ret) { goto out; }

    memcpy(sample->raw, rx, sizeof(rx));
    sample->status = ((uint32_t)rx[0] << 16) | ((uint32_t)rx[1] << 8) | rx[2];
    sample->ch1 = sign_extend_24(&rx[3]);
    sample->ch2 = sign_extend_24(&rx[6]);

out:
    k_mutex_unlock(&lock);
    return ret;
}
