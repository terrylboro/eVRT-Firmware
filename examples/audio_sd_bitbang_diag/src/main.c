#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>

#include <errno.h>

#define SD_GPIO_PORT DT_NODELABEL(gpio1)
#define SD_SCK_PIN 4
#define SD_MOSI_PIN 5
#define SD_MISO_PIN 6
#define SD_CS_PIN 7
#define SD_PIN_IDENTITY_TEST_ENABLED 0
#define SD_GPIO_DIAGNOSTIC_ENABLED 0
#define SD_PRECLOCK_BEFORE_DRIVER_ENABLED 0
#define SD_BITBANG_CMD0_DIAGNOSTIC_ENABLED 0
#define SD_BITBANG_INIT_DIAGNOSTIC_ENABLED 0
#define SD_BITBANG_READ_SECTOR0_ENABLED 0
#define SD_BITBANG_SAMPLE_SWEEP_ENABLED 1
#define SD_STOP_AFTER_BITBANG_CMD0 0
#define SD_STOP_AFTER_BITBANG_INIT 0
#define SD_STOP_AFTER_BITBANG_READ 1
#define SD_STOP_AFTER_SAMPLE_SWEEP 1
#define SD_BITBANG_HALF_PERIOD_US 100
#define SD_BITBANG_CMD0_ATTEMPTS 10
#define SD_BITBANG_CMD0_RESPONSE_BYTES 16
#define SD_BITBANG_PRE_CMD0_CLOCKS 80
#define SD_BITBANG_ACMD41_ATTEMPTS 40
#define SD_BITBANG_READ_TOKEN_ATTEMPTS 4000

static uint8_t sector0[512];

enum sd_bb_sample_mode {
    SD_BB_SAMPLE_RISING_DELAYED,
    SD_BB_SAMPLE_RISING_IMMEDIATE,
    SD_BB_SAMPLE_FALLING_DELAYED,
    SD_BB_SAMPLE_BEFORE_RISING,
};

static enum sd_bb_sample_mode sd_sample_mode = SD_BB_SAMPLE_RISING_DELAYED;

static const char *sd_sample_mode_name(enum sd_bb_sample_mode mode)
{
    switch (mode) {
    case SD_BB_SAMPLE_RISING_DELAYED:
        return "rising-delayed";
    case SD_BB_SAMPLE_RISING_IMMEDIATE:
        return "rising-immediate";
    case SD_BB_SAMPLE_FALLING_DELAYED:
        return "falling-delayed";
    case SD_BB_SAMPLE_BEFORE_RISING:
        return "before-rising";
    default:
        return "unknown";
    }
}

static const struct device *sd_gpio_dev(void)
{
    return DEVICE_DT_GET(SD_GPIO_PORT);
}

static void sd_pin_identity_test(void)
{
    const struct device *gpio1 = sd_gpio_dev();
    const uint8_t pins[] = { SD_SCK_PIN, SD_MOSI_PIN, SD_MISO_PIN, SD_CS_PIN };
    const char *names[] = { "P1.04 SCK", "P1.05 MOSI", "P1.06 MISO", "P1.07 CS" };

    if (!device_is_ready(gpio1)) {
        printk("SD pin identity test skipped: gpio1 not ready\n");
        return;
    }

    printk("SD pin identity test: one pin pulses at a time\n");

    for (size_t i = 0; i < ARRAY_SIZE(pins); i++) {
        gpio_pin_configure(gpio1, pins[i], GPIO_OUTPUT_INACTIVE);
    }

    k_sleep(K_MSEC(500));

    for (size_t i = 0; i < ARRAY_SIZE(pins); i++) {
        printk("  pulsing %s\n", names[i]);

        for (int pulse = 0; pulse < 5; pulse++) {
            gpio_pin_set(gpio1, pins[i], 1);
            k_sleep(K_MSEC(200));
            gpio_pin_set(gpio1, pins[i], 0);
            k_sleep(K_MSEC(200));
        }

        k_sleep(K_MSEC(500));
    }

    for (size_t i = 0; i < ARRAY_SIZE(pins); i++) {
        gpio_pin_configure(gpio1, pins[i], GPIO_DISCONNECTED);
    }

    printk("SD pin identity test complete\n");
}

static void sd_gpio_trace_pulse(void)
{
    const struct device *gpio1 = DEVICE_DT_GET(SD_GPIO_PORT);
    const uint8_t pins[] = { SD_SCK_PIN, SD_MOSI_PIN, SD_MISO_PIN, SD_CS_PIN };
    const char *names[] = { "P1.04 SCK", "P1.05 MOSI", "P1.06 MISO", "P1.07 CS" };

    if (!device_is_ready(gpio1)) {
        printk("SD GPIO diagnostic skipped: gpio1 not ready\n");
        return;
    }

    printk("SD GPIO diagnostic: quick pulse on P1.04/P1.05/P1.06/P1.07 before SPI init\n");

    for (size_t i = 0; i < ARRAY_SIZE(pins); i++) {
        int ret = gpio_pin_configure(gpio1, pins[i], GPIO_OUTPUT_INACTIVE);

        printk("  configure %s as output low: %d\n", names[i], ret);
    }

    k_sleep(K_MSEC(10));

    for (size_t i = 0; i < ARRAY_SIZE(pins); i++) {
        gpio_pin_set(gpio1, pins[i], 1);
        k_sleep(K_MSEC(25));
        gpio_pin_set(gpio1, pins[i], 0);
        k_sleep(K_MSEC(10));
    }

    for (int cycle = 0; cycle < 2; cycle++) {
        gpio_pin_set(gpio1, SD_SCK_PIN, 1);
        gpio_pin_set(gpio1, SD_MOSI_PIN, cycle & 1);
        gpio_pin_set(gpio1, SD_MISO_PIN, (cycle + 1) & 1);
        gpio_pin_set(gpio1, SD_CS_PIN, 0);
        k_sleep(K_MSEC(25));

        gpio_pin_set(gpio1, SD_SCK_PIN, 0);
        gpio_pin_set(gpio1, SD_MOSI_PIN, (cycle + 1) & 1);
        gpio_pin_set(gpio1, SD_MISO_PIN, cycle & 1);
        gpio_pin_set(gpio1, SD_CS_PIN, 1);
        k_sleep(K_MSEC(25));
    }

    for (size_t i = 0; i < ARRAY_SIZE(pins); i++) {
        int ret = gpio_pin_configure(gpio1, pins[i], GPIO_DISCONNECTED);

        printk("  release %s: %d\n", names[i], ret);
    }

    printk("SD GPIO diagnostic complete\n");
    k_sleep(K_MSEC(20));
}

static void sd_preclock_before_driver(void)
{
    const struct device *gpio1 = sd_gpio_dev();
    int ret;

    if (!device_is_ready(gpio1)) {
        printk("SD preclock skipped: gpio1 not ready\n");
        return;
    }

    printk("SD preclock: 160 clocks with CS high and DI high before Zephyr SD init\n");

    ret = gpio_pin_configure(gpio1, SD_SCK_PIN, GPIO_OUTPUT_LOW);
    ret |= gpio_pin_configure(gpio1, SD_MOSI_PIN, GPIO_OUTPUT_HIGH);
    ret |= gpio_pin_configure(gpio1, SD_MISO_PIN, GPIO_INPUT | GPIO_PULL_UP);
    ret |= gpio_pin_configure(gpio1, SD_CS_PIN, GPIO_OUTPUT_HIGH);
    printk("SD preclock configure ret=%d\n", ret);
    if (ret) {
        return;
    }

    k_sleep(K_MSEC(10));

    for (int i = 0; i < 160; i++) {
        gpio_pin_set_raw(gpio1, SD_SCK_PIN, 1);
        k_busy_wait(5);
        gpio_pin_set_raw(gpio1, SD_SCK_PIN, 0);
        k_busy_wait(5);
    }

    gpio_pin_configure(gpio1, SD_SCK_PIN, GPIO_DISCONNECTED);
    gpio_pin_configure(gpio1, SD_MOSI_PIN, GPIO_DISCONNECTED);
    gpio_pin_configure(gpio1, SD_MISO_PIN, GPIO_DISCONNECTED);
    gpio_pin_configure(gpio1, SD_CS_PIN, GPIO_DISCONNECTED);
    k_sleep(K_MSEC(20));
}

static uint8_t sd_bb_transfer_byte(const struct device *gpio1, uint8_t tx)
{
    uint8_t rx = 0;

    for (int bit = 7; bit >= 0; bit--) {
        int sample = 0;

        gpio_pin_set_raw(gpio1, SD_MOSI_PIN, (tx >> bit) & 1U);
        k_busy_wait(SD_BITBANG_HALF_PERIOD_US);

        if (sd_sample_mode == SD_BB_SAMPLE_BEFORE_RISING) {
            sample = gpio_pin_get(gpio1, SD_MISO_PIN) > 0;
        }

        gpio_pin_set_raw(gpio1, SD_SCK_PIN, 1);

        if (sd_sample_mode == SD_BB_SAMPLE_RISING_IMMEDIATE) {
            sample = gpio_pin_get(gpio1, SD_MISO_PIN) > 0;
        }

        k_busy_wait(SD_BITBANG_HALF_PERIOD_US);

        if (sd_sample_mode == SD_BB_SAMPLE_RISING_DELAYED) {
            sample = gpio_pin_get(gpio1, SD_MISO_PIN) > 0;
        }

        gpio_pin_set_raw(gpio1, SD_SCK_PIN, 0);

        if (sd_sample_mode == SD_BB_SAMPLE_FALLING_DELAYED) {
            k_busy_wait(SD_BITBANG_HALF_PERIOD_US);
            sample = gpio_pin_get(gpio1, SD_MISO_PIN) > 0;
        }

        rx <<= 1;
        if (sample) {
            rx |= 1U;
        }

        k_busy_wait(SD_BITBANG_HALF_PERIOD_US);
    }

    return rx;
}

static size_t sd_bb_read_response(const struct device *gpio1,
                                  uint8_t *response,
                                  size_t response_size)
{
    for (size_t i = 0; i < response_size; i++) {
        response[i] = sd_bb_transfer_byte(gpio1, 0xff);
        if (response[i] != 0xff) {
            return i + 1U;
        }
    }

    return response_size;
}

static size_t sd_bb_send_cmd(const struct device *gpio1,
                             uint8_t cmd,
                             uint32_t arg,
                             uint8_t crc,
                             uint8_t *response,
                             size_t response_size)
{
    uint8_t packet[] = {
        (uint8_t)(0x40U | cmd),
        (uint8_t)(arg >> 24),
        (uint8_t)(arg >> 16),
        (uint8_t)(arg >> 8),
        (uint8_t)arg,
        crc,
    };

    for (size_t i = 0; i < ARRAY_SIZE(packet); i++) {
        (void)sd_bb_transfer_byte(gpio1, packet[i]);
    }

    return sd_bb_read_response(gpio1, response, response_size);
}

static size_t sd_bb_command_transaction(const struct device *gpio1,
                                        uint8_t cmd,
                                        uint32_t arg,
                                        uint8_t crc,
                                        uint8_t *response,
                                        size_t response_size)
{
    size_t len;

    gpio_pin_set_raw(gpio1, SD_CS_PIN, 0);
    k_busy_wait(100);

    (void)sd_bb_transfer_byte(gpio1, 0xff);
    len = sd_bb_send_cmd(gpio1, cmd, arg, crc, response, response_size);

    gpio_pin_set_raw(gpio1, SD_CS_PIN, 1);
    (void)sd_bb_transfer_byte(gpio1, 0xff);

    return len;
}

static size_t sd_bb_command_transaction_extra(const struct device *gpio1,
                                              uint8_t cmd,
                                              uint32_t arg,
                                              uint8_t crc,
                                              uint8_t *response,
                                              size_t response_size,
                                              size_t extra_bytes)
{
    size_t len;

    gpio_pin_set_raw(gpio1, SD_CS_PIN, 0);
    k_busy_wait(100);

    (void)sd_bb_transfer_byte(gpio1, 0xff);
    len = sd_bb_send_cmd(gpio1, cmd, arg, crc, response, response_size);

    while (len < response_size && extra_bytes > 0U) {
        response[len++] = sd_bb_transfer_byte(gpio1, 0xff);
        extra_bytes--;
    }

    gpio_pin_set_raw(gpio1, SD_CS_PIN, 1);
    (void)sd_bb_transfer_byte(gpio1, 0xff);

    return len;
}

static void sd_print_response(const char *label, size_t len, const uint8_t *response)
{
    printk("%s", label);
    for (size_t i = 0; i < len; i++) {
        printk(" %02x", response[i]);
    }
    printk("\n");
}

static int sd_bitbang_configure_bus(const struct device *gpio1)
{
    int ret;

    ret = gpio_pin_configure(gpio1, SD_SCK_PIN, GPIO_OUTPUT_LOW);
    ret |= gpio_pin_configure(gpio1, SD_MOSI_PIN, GPIO_OUTPUT_HIGH);
    ret |= gpio_pin_configure(gpio1, SD_MISO_PIN, GPIO_INPUT | GPIO_PULL_UP);
    ret |= gpio_pin_configure(gpio1, SD_CS_PIN, GPIO_OUTPUT_HIGH);
    if (ret) {
        return ret;
    }

    k_sleep(K_MSEC(250));

    gpio_pin_set_raw(gpio1, SD_CS_PIN, 1);
    k_sleep(K_MSEC(20));

    for (int i = 0; i < SD_BITBANG_PRE_CMD0_CLOCKS / 8; i++) {
        (void)sd_bb_transfer_byte(gpio1, 0xff);
    }

    k_sleep(K_MSEC(20));
    (void)sd_bb_transfer_byte(gpio1, 0xff);

    return 0;
}

static void sd_bitbang_release_bus(const struct device *gpio1)
{
    gpio_pin_configure(gpio1, SD_SCK_PIN, GPIO_DISCONNECTED);
    gpio_pin_configure(gpio1, SD_MOSI_PIN, GPIO_DISCONNECTED);
    gpio_pin_configure(gpio1, SD_MISO_PIN, GPIO_DISCONNECTED);
    gpio_pin_configure(gpio1, SD_CS_PIN, GPIO_DISCONNECTED);
    k_sleep(K_MSEC(20));
}

static void sd_bitbang_cmd0_trace(void)
{
    const struct device *gpio1 = sd_gpio_dev();
    uint8_t response[SD_BITBANG_CMD0_RESPONSE_BYTES];
    int ret;

    if (!device_is_ready(gpio1)) {
        return;
    }

    ret = sd_bitbang_configure_bus(gpio1);
    if (ret) {
        return;
    }

    for (int attempt = 1; attempt <= SD_BITBANG_CMD0_ATTEMPTS; attempt++) {
        size_t response_len;

        gpio_pin_set_raw(gpio1, SD_CS_PIN, 0);
        k_busy_wait(100);

        response_len = sd_bb_send_cmd(gpio1, 0, 0x00000000, 0x95,
                                      response, ARRAY_SIZE(response));

        gpio_pin_set_raw(gpio1, SD_CS_PIN, 1);
        (void)sd_bb_transfer_byte(gpio1, 0xff);

        if (response[response_len - 1U] != 0xff) {
            break;
        }

        k_sleep(K_MSEC(10));
    }

    sd_bitbang_release_bus(gpio1);
}

static void sd_bitbang_init_trace(void)
{
    const struct device *gpio1 = sd_gpio_dev();
    uint8_t response[32];
    size_t len;
    int ret;

    if (!device_is_ready(gpio1)) {
        return;
    }

    ret = sd_bitbang_configure_bus(gpio1);
    if (ret) {
        return;
    }

    len = sd_bb_command_transaction(gpio1, 0, 0x00000000, 0x95,
                                    response, ARRAY_SIZE(response));
    sd_print_response("  CMD0:", len, response);

    len = sd_bb_command_transaction_extra(gpio1, 8, 0x000001aa, 0x87,
                                          response, ARRAY_SIZE(response), 4);
    sd_print_response("  CMD8:", len, response);

    for (int attempt = 1; attempt <= SD_BITBANG_ACMD41_ATTEMPTS; attempt++) {
        len = sd_bb_command_transaction(gpio1, 55, 0x00000000, 0x65,
                                        response, ARRAY_SIZE(response));
        if (attempt <= 5 || (attempt % 5) == 0) {
            sd_print_response("  CMD55:", len, response);
        }

        len = sd_bb_command_transaction(gpio1, 41, 0x40000000, 0x77,
                                        response, ARRAY_SIZE(response));
        if (attempt <= 5 || (attempt % 5) == 0 || response[len - 1U] == 0x00) {
            sd_print_response("  ACMD41:", len, response);
        }

        if (response[len - 1U] == 0x00) {
            break;
        }

        k_sleep(K_MSEC(50));
    }

    len = sd_bb_command_transaction_extra(gpio1, 58, 0x00000000, 0xfd,
                                          response, ARRAY_SIZE(response), 4);
    sd_print_response("  CMD58:", len, response);

    sd_bitbang_release_bus(gpio1);
}

static int sd_bitbang_init_card(const struct device *gpio1)
{
    uint8_t response[32];
    size_t len;
    int ret;

    ret = sd_bitbang_configure_bus(gpio1);
    if (ret) {
        printk("  configure bus failed: %d\n", ret);
        return ret;
    }

    len = sd_bb_command_transaction(gpio1, 0, 0x00000000, 0x95,
                                    response, ARRAY_SIZE(response));
    sd_print_response("  CMD0:", len, response);
    if (response[len - 1U] != 0x01) {
        return -EIO;
    }

    len = sd_bb_command_transaction_extra(gpio1, 8, 0x000001aa, 0x87,
                                          response, ARRAY_SIZE(response), 4);
    sd_print_response("  CMD8:", len, response);
    if (len < 5 || response[0] != 0x01 || response[3] != 0x01 || response[4] != 0xaa) {
        printk("  CMD8 unexpected; continuing anyway for diagnostic\n");
    }

    for (int attempt = 1; attempt <= SD_BITBANG_ACMD41_ATTEMPTS; attempt++) {
        len = sd_bb_command_transaction(gpio1, 55, 0x00000000, 0x65,
                                        response, ARRAY_SIZE(response));
        if (attempt <= 3 || (attempt % 5) == 0) {
            sd_print_response("  CMD55:", len, response);
        }

        len = sd_bb_command_transaction(gpio1, 41, 0x40000000, 0x77,
                                        response, ARRAY_SIZE(response));
        if (attempt <= 3 || (attempt % 5) == 0 || response[len - 1U] == 0x00) {
            sd_print_response("  ACMD41:", len, response);
        }

        if (response[len - 1U] == 0x00) {
            len = sd_bb_command_transaction_extra(gpio1, 58, 0x00000000, 0xfd,
                                                  response, ARRAY_SIZE(response), 4);
            sd_print_response("  CMD58:", len, response);
            return 0;
        }

        k_sleep(K_MSEC(50));
    }

    return -ETIMEDOUT;
}

static int sd_bitbang_read_sector0(void)
{
    const struct device *gpio1 = sd_gpio_dev();
    uint8_t response[32];
    uint8_t crc_hi;
    uint8_t crc_lo;
    uint8_t token = 0xff;
    uint32_t sum = 0;
    size_t len;
    int ret;

    if (!device_is_ready(gpio1)) {
        return -ENODEV;
    }

    printk("SD bitbang sector0 read diagnostic\n");

    ret = sd_bitbang_init_card(gpio1);
    if (ret) {
        printk("  init failed: %d\n", ret);
        sd_bitbang_release_bus(gpio1);
        return ret;
    }

    gpio_pin_set_raw(gpio1, SD_CS_PIN, 0);
    k_busy_wait(100);
    (void)sd_bb_transfer_byte(gpio1, 0xff);

    len = sd_bb_send_cmd(gpio1, 17, 0x00000000, 0xff,
                         response, ARRAY_SIZE(response));
    sd_print_response("  CMD17:", len, response);
    if (response[len - 1U] != 0x00) {
        gpio_pin_set_raw(gpio1, SD_CS_PIN, 1);
        (void)sd_bb_transfer_byte(gpio1, 0xff);
        sd_bitbang_release_bus(gpio1);
        return -EIO;
    }

    for (int attempt = 0; attempt < SD_BITBANG_READ_TOKEN_ATTEMPTS; attempt++) {
        token = sd_bb_transfer_byte(gpio1, 0xff);
        if (token != 0xff) {
            break;
        }
    }

    printk("  data token: %02x\n", token);
    if (token != 0xfe) {
        gpio_pin_set_raw(gpio1, SD_CS_PIN, 1);
        (void)sd_bb_transfer_byte(gpio1, 0xff);
        sd_bitbang_release_bus(gpio1);
        return -EIO;
    }

    for (size_t i = 0; i < ARRAY_SIZE(sector0); i++) {
        sector0[i] = sd_bb_transfer_byte(gpio1, 0xff);
        sum += sector0[i];
    }

    crc_hi = sd_bb_transfer_byte(gpio1, 0xff);
    crc_lo = sd_bb_transfer_byte(gpio1, 0xff);

    gpio_pin_set_raw(gpio1, SD_CS_PIN, 1);
    (void)sd_bb_transfer_byte(gpio1, 0xff);

    printk("  data crc bytes: %02x %02x\n", crc_hi, crc_lo);
    printk("  sector0 byte sum: %08x\n", sum);
    printk("  sector0 first 64:");
    for (size_t i = 0; i < 64; i++) {
        printk(" %02x", sector0[i]);
    }
    printk("\n");

    sd_bitbang_release_bus(gpio1);
    return 0;
}

static int sd_bitbang_probe_read_token(void)
{
    const struct device *gpio1 = sd_gpio_dev();
    uint8_t response[32];
    uint8_t token = 0xff;
    size_t len;
    int ret;

    if (!device_is_ready(gpio1)) {
        return -ENODEV;
    }

    ret = sd_bitbang_init_card(gpio1);
    if (ret) {
        printk("  init failed: %d\n", ret);
        sd_bitbang_release_bus(gpio1);
        return ret;
    }

    gpio_pin_set_raw(gpio1, SD_CS_PIN, 0);
    k_busy_wait(100);
    (void)sd_bb_transfer_byte(gpio1, 0xff);

    len = sd_bb_send_cmd(gpio1, 17, 0x00000000, 0xff,
                         response, ARRAY_SIZE(response));
    sd_print_response("  CMD17:", len, response);

    if (response[len - 1U] == 0x00) {
        for (int attempt = 0; attempt < SD_BITBANG_READ_TOKEN_ATTEMPTS; attempt++) {
            token = sd_bb_transfer_byte(gpio1, 0xff);
            if (token != 0xff) {
                break;
            }
        }
    }

    printk("  data token: %02x\n", token);

    gpio_pin_set_raw(gpio1, SD_CS_PIN, 1);
    (void)sd_bb_transfer_byte(gpio1, 0xff);
    sd_bitbang_release_bus(gpio1);

    return (response[len - 1U] == 0x00 && token == 0xfe) ? 0 : -EIO;
}

static void sd_bitbang_sample_sweep(void)
{
    const enum sd_bb_sample_mode modes[] = {
        SD_BB_SAMPLE_RISING_DELAYED,
        SD_BB_SAMPLE_RISING_IMMEDIATE,
        SD_BB_SAMPLE_FALLING_DELAYED,
        SD_BB_SAMPLE_BEFORE_RISING,
    };

    for (size_t i = 0; i < ARRAY_SIZE(modes); i++) {
        int ret;

        sd_sample_mode = modes[i];
        printk("SD sample mode: %s\n", sd_sample_mode_name(sd_sample_mode));
        ret = sd_bitbang_probe_read_token();
        printk("  sample mode result: %d\n", ret);
        k_sleep(K_MSEC(250));
    }
}

int main(void)
{
    printk("SD bitbang diagnostic baseline\n");

#if SD_PIN_IDENTITY_TEST_ENABLED
    sd_pin_identity_test();
    printk("Stopping after SD pin identity test\n");
    return 0;
#endif

#if SD_GPIO_DIAGNOSTIC_ENABLED
    sd_gpio_trace_pulse();
#endif
#if SD_PRECLOCK_BEFORE_DRIVER_ENABLED
    sd_preclock_before_driver();
#endif
#if SD_BITBANG_CMD0_DIAGNOSTIC_ENABLED
    sd_bitbang_cmd0_trace();
#if SD_STOP_AFTER_BITBANG_CMD0
    printk("Stopping after SD bitbang CMD0 diagnostic\n");
    return 0;
#endif
#endif
#if SD_BITBANG_INIT_DIAGNOSTIC_ENABLED
    sd_bitbang_init_trace();
#if SD_STOP_AFTER_BITBANG_INIT
    printk("Stopping after SD bitbang init diagnostic\n");
    return 0;
#endif
#endif
#if SD_BITBANG_READ_SECTOR0_ENABLED
    int ret = sd_bitbang_read_sector0();
    printk("SD bitbang sector0 read result: %d\n", ret);
#if SD_STOP_AFTER_BITBANG_READ
    printk("Stopping after SD bitbang sector0 read diagnostic\n");
    return 0;
#endif
#endif
#if SD_BITBANG_SAMPLE_SWEEP_ENABLED
    sd_bitbang_sample_sweep();
#if SD_STOP_AFTER_SAMPLE_SWEEP
    printk("Stopping after SD bitbang sample sweep diagnostic\n");
    return 0;
#endif
#endif
    printk("SD bitbang diagnostic complete\n");
    return 0;
}
