#include <errno.h>
#include <math.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/drivers/i2s.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#define SAMPLE_RATE_HZ 44100U
#define TONE_HZ 440U
#define CHANNELS 2U
#define WORD_SIZE_BITS 16U
#define BLOCK_FRAMES 256U
#define BLOCK_SIZE (BLOCK_FRAMES * CHANNELS * sizeof(int16_t))
#define NUM_BLOCKS 8U
#define PLAY_SECONDS 10U
#define AMPLITUDE 24000
#define TWO_PI 6.28318530717958647692

#ifdef CONFIG_NOCACHE_MEMORY
#define MEM_SLAB_CACHE_ATTR __nocache
#else
#define MEM_SLAB_CACHE_ATTR
#endif

static char MEM_SLAB_CACHE_ATTR __aligned(WB_UP(32))
    tx_mem_slab_buffer[NUM_BLOCKS * WB_UP(BLOCK_SIZE)];

static STRUCT_SECTION_ITERABLE(k_mem_slab, tx_mem_slab) =
    Z_MEM_SLAB_INITIALIZER(tx_mem_slab, tx_mem_slab_buffer,
                           WB_UP(BLOCK_SIZE), NUM_BLOCKS);

static int fill_sine_block(void *block, uint32_t *phase)
{
    int16_t *samples = block;

    for (uint32_t frame = 0; frame < BLOCK_FRAMES; frame++) {
        double radians = (TWO_PI * (double)(*phase)) / (double)SAMPLE_RATE_HZ;
        int16_t sample = (int16_t)(sin(radians) * AMPLITUDE);

        samples[(frame * 2U) + 0U] = sample;
        samples[(frame * 2U) + 1U] = sample;

        *phase += TONE_HZ;
        if (*phase >= SAMPLE_RATE_HZ) {
            *phase -= SAMPLE_RATE_HZ;
        }
    }

    return 0;
}

static int write_i2s_block(const struct device *i2s_dev, void *block)
{
    int ret;

    do {
        ret = i2s_write(i2s_dev, block, BLOCK_SIZE);
        if (ret == -EAGAIN) {
            k_sleep(K_MSEC(1));
        }
    } while (ret == -EAGAIN);

    return ret;
}

int main(void)
{
    const struct device *i2s_dev = DEVICE_DT_GET(DT_ALIAS(i2s_tx));
    struct i2s_config cfg = {
        .word_size = WORD_SIZE_BITS,
        .channels = CHANNELS,
        .format = I2S_FMT_DATA_FORMAT_I2S,
        .frame_clk_freq = SAMPLE_RATE_HZ,
        .block_size = BLOCK_SIZE,
        .timeout = 2000,
        .options = I2S_OPT_FRAME_CLK_MASTER | I2S_OPT_BIT_CLK_MASTER,
        .mem_slab = &tx_mem_slab,
    };
    uint32_t phase = 0U;
    uint32_t blocks_total =
        (SAMPLE_RATE_HZ * PLAY_SECONDS + BLOCK_FRAMES - 1U) / BLOCK_FRAMES;
    int ret;

    printk("MAX98357A I2S sine test: %u Hz for %u seconds\n",
           TONE_HZ, PLAY_SECONDS);

    if (!device_is_ready(i2s_dev)) {
        printk("I2S device not ready\n");
        return -ENODEV;
    }

    printk("I2S device: %s\n", i2s_dev->name);

    ret = i2s_configure(i2s_dev, I2S_DIR_TX, &cfg);
    if (ret < 0) {
        printk("I2S configure failed: %d\n", ret);
        return ret;
    }

    for (uint32_t i = 0; i < 2U; i++) {
        void *block;

        ret = k_mem_slab_alloc(&tx_mem_slab, &block, K_FOREVER);
        if (ret < 0) {
            printk("Initial TX alloc failed: %d\n", ret);
            return ret;
        }

        fill_sine_block(block, &phase);

        ret = write_i2s_block(i2s_dev, block);
        if (ret < 0) {
            printk("Initial I2S write failed: %d\n", ret);
            k_mem_slab_free(&tx_mem_slab, block);
            return ret;
        }
    }

    ret = i2s_trigger(i2s_dev, I2S_DIR_TX, I2S_TRIGGER_START);
    if (ret < 0) {
        printk("I2S start failed: %d\n", ret);
        return ret;
    }

    for (uint32_t i = 2U; i < blocks_total; i++) {
        void *block;

        ret = k_mem_slab_alloc(&tx_mem_slab, &block, K_FOREVER);
        if (ret < 0) {
            printk("TX alloc failed: %d\n", ret);
            break;
        }

        fill_sine_block(block, &phase);

        ret = write_i2s_block(i2s_dev, block);
        if (ret < 0) {
            printk("I2S write failed at block %u/%u: %d\n",
                   i, blocks_total, ret);
            k_mem_slab_free(&tx_mem_slab, block);
            break;
        }
    }

    int drain_ret = i2s_trigger(i2s_dev, I2S_DIR_TX, I2S_TRIGGER_DRAIN);

    if (drain_ret < 0) {
        printk("I2S drain failed: %d\n", drain_ret);
        if (ret == 0) {
            ret = drain_ret;
        }
    }

    printk("Sine test complete: %d\n", ret);
    return ret;
}
