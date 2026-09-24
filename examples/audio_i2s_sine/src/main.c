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
#define BLOCK_FRAMES 1024U
#define BLOCK_SIZE (BLOCK_FRAMES * CHANNELS * sizeof(int16_t))
#define NUM_BLOCKS 16U
#define PLAY_SECONDS 10U
#define AMPLITUDE 24000
#define TWO_PI 6.28318530717958647692
#define SINE_TABLE_SIZE 256U
#define PREFILL_BLOCKS 4U
#define I2S_WRITE_RETRY_LOG_MS 1000

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

static int16_t sine_table[SINE_TABLE_SIZE];

static void init_sine_table(void)
{
    for (uint32_t i = 0; i < SINE_TABLE_SIZE; i++) {
        double radians = (TWO_PI * (double)i) / (double)SINE_TABLE_SIZE;

        sine_table[i] = (int16_t)(sin(radians) * AMPLITUDE);
    }
}

static int fill_sine_block(void *block, uint32_t *phase)
{
    int16_t *samples = block;
    uint32_t phase_step =
        (uint32_t)(((uint64_t)TONE_HZ << 32) / SAMPLE_RATE_HZ);

    for (uint32_t frame = 0; frame < BLOCK_FRAMES; frame++) {
        uint32_t table_index = *phase >> 24;
        int16_t sample = sine_table[table_index];

        samples[(frame * 2U) + 0U] = sample;
        samples[(frame * 2U) + 1U] = sample;

        *phase += phase_step;
    }

    return 0;
}

static int write_i2s_block(const struct device *i2s_dev, void *block,
                           uint32_t block_index, uint32_t *retry_count)
{
    int64_t retry_log_time = k_uptime_get() + I2S_WRITE_RETRY_LOG_MS;
    int ret;

    do {
        ret = i2s_write(i2s_dev, block, BLOCK_SIZE);
        if (ret == -EAGAIN) {
            (*retry_count)++;
            if (k_uptime_get() >= retry_log_time) {
                printk("I2S write busy at block %u, retries=%u\n",
                       block_index, *retry_count);
                retry_log_time = k_uptime_get() + I2S_WRITE_RETRY_LOG_MS;
            }
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
    uint32_t blocks_written = 0U;
    uint32_t write_retries = 0U;
    int64_t start_time;
    int64_t stream_start_time;
    int64_t stream_end_time;
    int64_t next_progress_time;
    int ret;

    start_time = k_uptime_get();
    printk("MAX98357A I2S sine test: %u Hz for %u seconds\n",
           TONE_HZ, PLAY_SECONDS);
    printk("I2S config request: sample_rate=%u block_frames=%u block_size=%u blocks=%u\n",
           SAMPLE_RATE_HZ, BLOCK_FRAMES, BLOCK_SIZE, blocks_total);
    init_sine_table();

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
    printk("I2S configured at %lld ms\n", k_uptime_get() - start_time);

    for (uint32_t i = 0; i < PREFILL_BLOCKS; i++) {
        void *block;

        ret = k_mem_slab_alloc(&tx_mem_slab, &block, K_FOREVER);
        if (ret < 0) {
            printk("Initial TX alloc failed: %d\n", ret);
            return ret;
        }

        fill_sine_block(block, &phase);

        ret = write_i2s_block(i2s_dev, block, i, &write_retries);
        if (ret < 0) {
            printk("Initial I2S write failed: %d\n", ret);
            k_mem_slab_free(&tx_mem_slab, block);
            return ret;
        }
        blocks_written++;
        printk("I2S prefilled block %u/%u\n", blocks_written, blocks_total);
    }

    ret = i2s_trigger(i2s_dev, I2S_DIR_TX, I2S_TRIGGER_START);
    if (ret < 0) {
        printk("I2S start failed: %d\n", ret);
        return ret;
    }
    stream_start_time = k_uptime_get();
    stream_end_time = stream_start_time + (PLAY_SECONDS * 1000);
    next_progress_time = stream_start_time + 1000;
    printk("I2S started at %lld ms\n", stream_start_time - start_time);

    for (uint32_t i = PREFILL_BLOCKS; k_uptime_get() < stream_end_time; i++) {
        void *block;

        ret = k_mem_slab_alloc(&tx_mem_slab, &block, K_FOREVER);
        if (ret < 0) {
            printk("TX alloc failed: %d\n", ret);
            break;
        }

        fill_sine_block(block, &phase);

        ret = write_i2s_block(i2s_dev, block, i, &write_retries);
        if (ret < 0) {
            printk("I2S write failed at block %u/%u: %d\n",
                   i, blocks_total, ret);
            k_mem_slab_free(&tx_mem_slab, block);
            break;
        }
        blocks_written++;

        if (k_uptime_get() >= next_progress_time) {
            printk("I2S streaming: elapsed=%lld ms blocks=%u target_blocks=%u retries=%u\n",
                   k_uptime_get() - stream_start_time, blocks_written,
                   blocks_total, write_retries);
            next_progress_time += 1000;
        }

    }

    printk("I2S stream loop exit: elapsed=%lld ms blocks=%u target_blocks=%u ret=%d retries=%u\n",
           k_uptime_get() - stream_start_time, blocks_written, blocks_total,
           ret, write_retries);
    printk("I2S drain start\n");
    int drain_ret = i2s_trigger(i2s_dev, I2S_DIR_TX, I2S_TRIGGER_DRAIN);

    if (drain_ret < 0) {
        printk("I2S drain failed: %d\n", drain_ret);
        if (ret == 0) {
            ret = drain_ret;
        }
    }
    printk("I2S drain complete: %d elapsed=%lld ms\n",
           drain_ret, k_uptime_get() - stream_start_time);

    printk("Sine test complete: %d\n", ret);
    return ret;
}
