#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>

#define BB_SCK_NODE   DT_ALIAS(ads_bb_sck)
#define BB_MOSI_NODE  DT_ALIAS(ads_bb_mosi)
#define BB_MISO_NODE  DT_ALIAS(ads_bb_miso)
#define BB_CS_NODE    DT_ALIAS(ads_bb_cs)
#define BB_START_NODE DT_ALIAS(ads_bb_start)
#define BB_RESET_NODE DT_ALIAS(ads_bb_reset)

#if !DT_NODE_HAS_STATUS(BB_SCK_NODE, okay) || !DT_NODE_HAS_STATUS(BB_MOSI_NODE, okay) || \
    !DT_NODE_HAS_STATUS(BB_MISO_NODE, okay) || !DT_NODE_HAS_STATUS(BB_CS_NODE, okay) || \
    !DT_NODE_HAS_STATUS(BB_START_NODE, okay) || !DT_NODE_HAS_STATUS(BB_RESET_NODE, okay)
#error "ADS1292R bitbang example requires ads-bb-* aliases in the active overlay"
#endif

#define BB_SCK_PORT_NODE   DT_GPIO_CTLR(BB_SCK_NODE, gpios)
#define BB_SCK_PIN         DT_GPIO_PIN(BB_SCK_NODE, gpios)
#define BB_MOSI_PORT_NODE  DT_GPIO_CTLR(BB_MOSI_NODE, gpios)
#define BB_MOSI_PIN        DT_GPIO_PIN(BB_MOSI_NODE, gpios)
#define BB_MISO_PORT_NODE  DT_GPIO_CTLR(BB_MISO_NODE, gpios)
#define BB_MISO_PIN        DT_GPIO_PIN(BB_MISO_NODE, gpios)
#define BB_CS_PORT_NODE    DT_GPIO_CTLR(BB_CS_NODE, gpios)
#define BB_CS_PIN          DT_GPIO_PIN(BB_CS_NODE, gpios)
#define BB_START_PORT_NODE DT_GPIO_CTLR(BB_START_NODE, gpios)
#define BB_START_PIN       DT_GPIO_PIN(BB_START_NODE, gpios)
#define BB_RESET_PORT_NODE DT_GPIO_CTLR(BB_RESET_NODE, gpios)
#define BB_RESET_PIN       DT_GPIO_PIN(BB_RESET_NODE, gpios)

static const struct device *const bb_sck = DEVICE_DT_GET(BB_SCK_PORT_NODE);
static const struct device *const bb_mosi = DEVICE_DT_GET(BB_MOSI_PORT_NODE);
static const struct device *const bb_miso = DEVICE_DT_GET(BB_MISO_PORT_NODE);
static const struct device *const bb_cs = DEVICE_DT_GET(BB_CS_PORT_NODE);
static const struct device *const bb_start = DEVICE_DT_GET(BB_START_PORT_NODE);
static const struct device *const bb_reset = DEVICE_DT_GET(BB_RESET_PORT_NODE);

static void bb_transfer_byte(uint8_t tx, uint8_t *sample_high, uint8_t *sample_low)
{
    uint8_t high = 0;
    uint8_t low = 0;

    for (int bit = 7; bit >= 0; --bit) {
        gpio_pin_set(bb_mosi, BB_MOSI_PIN, (tx >> bit) & 1);
        k_busy_wait(100);

        gpio_pin_set(bb_sck, BB_SCK_PIN, 1);
        k_busy_wait(100);
        high = (high << 1) | (gpio_pin_get(bb_miso, BB_MISO_PIN) ? 1 : 0);

        gpio_pin_set(bb_sck, BB_SCK_PIN, 0);
        k_busy_wait(100);
        low = (low << 1) | (gpio_pin_get(bb_miso, BB_MISO_PIN) ? 1 : 0);
    }

    *sample_high = high;
    *sample_low = low;
}

static void bb_send_command(uint8_t command)
{
    gpio_pin_set(bb_cs, BB_CS_PIN, 0);
    k_busy_wait(100);
    uint8_t ignored_high, ignored_low;
    bb_transfer_byte(command, &ignored_high, &ignored_low);
    k_busy_wait(100);
    gpio_pin_set(bb_cs, BB_CS_PIN, 1);
    k_sleep(K_MSEC(5));
}

static void ads1292r_bitbang_id_test(void)
{
    printk("ADS1292R bitbang test: slow GPIO SPI, reading ID only\n");
    printk("ADS1292R bitbang pins: SCK P%u.%u MOSI P%u.%u MISO P%u.%u CS P%u.%u START P%u.%u /PWDN P%u.%u\n",
           (unsigned)DT_REG_ADDR(BB_SCK_PORT_NODE), BB_SCK_PIN,
           (unsigned)DT_REG_ADDR(BB_MOSI_PORT_NODE), BB_MOSI_PIN,
           (unsigned)DT_REG_ADDR(BB_MISO_PORT_NODE), BB_MISO_PIN,
           (unsigned)DT_REG_ADDR(BB_CS_PORT_NODE), BB_CS_PIN,
           (unsigned)DT_REG_ADDR(BB_START_PORT_NODE), BB_START_PIN,
           (unsigned)DT_REG_ADDR(BB_RESET_PORT_NODE), BB_RESET_PIN);

    if (!device_is_ready(bb_sck) || !device_is_ready(bb_mosi) ||
        !device_is_ready(bb_miso) || !device_is_ready(bb_cs) ||
        !device_is_ready(bb_start) || !device_is_ready(bb_reset)) {
        printk("ADS1292R bitbang test: GPIO device not ready\n");
        return;
    }

    gpio_pin_configure(bb_sck, BB_SCK_PIN, GPIO_OUTPUT_INACTIVE);
    gpio_pin_configure(bb_mosi, BB_MOSI_PIN, GPIO_OUTPUT_INACTIVE);
    gpio_pin_configure(bb_miso, BB_MISO_PIN, GPIO_INPUT | GPIO_PULL_UP);
    gpio_pin_configure(bb_cs, BB_CS_PIN, GPIO_OUTPUT_ACTIVE);
    gpio_pin_configure(bb_start, BB_START_PIN, GPIO_OUTPUT_INACTIVE);
    gpio_pin_configure(bb_reset, BB_RESET_PIN, GPIO_OUTPUT_INACTIVE);

    printk("ADS1292R bitbang test: XIAO-style /PWDN reset pulse\n");
    gpio_pin_set(bb_start, BB_START_PIN, 0);
    gpio_pin_set(bb_cs, BB_CS_PIN, 1);
    gpio_pin_set(bb_sck, BB_SCK_PIN, 0);
    gpio_pin_set(bb_mosi, BB_MOSI_PIN, 0);
    gpio_pin_set(bb_reset, BB_RESET_PIN, 0);
    k_sleep(K_MSEC(100));
    gpio_pin_set(bb_reset, BB_RESET_PIN, 1);
    k_sleep(K_SECONDS(1));

    printk("ADS1292R bitbang test: START low, sending SDATAC\n");
    gpio_pin_set(bb_start, BB_START_PIN, 0);
    k_sleep(K_MSEC(100));
    bb_send_command(0x11); /* SDATAC */

    while (1) {
        uint8_t h0, h1, h2;
        uint8_t l0, l1, l2;

        gpio_pin_set(bb_start, BB_START_PIN, 0);
        gpio_pin_set(bb_cs, BB_CS_PIN, 0);
        k_sleep(K_MSEC(2));
        bb_transfer_byte(0x20, &h0, &l0); /* RREG 0 */
        k_sleep(K_MSEC(2));
        bb_transfer_byte(0x00, &h1, &l1); /* one register */
        k_sleep(K_MSEC(2));
        bb_transfer_byte(0x00, &h2, &l2); /* read data */
        k_sleep(K_MSEC(2));
        gpio_pin_set(bb_cs, BB_CS_PIN, 1);

        printk("ADS1292R bitbang RX high-phase: %02x %02x %02x; low-phase: %02x %02x %02x\n",
               h0, h1, h2, l0, l1, l2);
        k_sleep(K_SECONDS(1));
    }
}

int main(void)
{
    printk("ADS1292R firmware build: bitbang-xiao-reset-sequence-test v3\n");
    ads1292r_bitbang_id_test();
    return 0;
}









