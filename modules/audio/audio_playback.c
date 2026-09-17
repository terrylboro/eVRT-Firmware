#include "audio_playback.h"

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <ff.h>
#include <zephyr/drivers/i2s.h>
#include <zephyr/fs/fs.h>
#include <zephyr/kernel.h>
#include <zephyr/storage/disk_access.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>

#define AUDIO_BLOCK_SIZE 2048U
#define AUDIO_NUM_BLOCKS 20U
#define AUDIO_SD_DISK_NAME "SD"
#define AUDIO_SD_MOUNT_POINT "/SD:"
#define AUDIO_SD_VERBOSE_DISK_PROBE 1

#ifdef CONFIG_NOCACHE_MEMORY
#define MEM_SLAB_CACHE_ATTR __nocache
#else
#define MEM_SLAB_CACHE_ATTR
#endif

static char MEM_SLAB_CACHE_ATTR __aligned(WB_UP(32))
    audio_tx_mem_slab_buffer[AUDIO_NUM_BLOCKS * WB_UP(AUDIO_BLOCK_SIZE)];

static STRUCT_SECTION_ITERABLE(k_mem_slab, audio_tx_mem_slab) =
    Z_MEM_SLAB_INITIALIZER(audio_tx_mem_slab, audio_tx_mem_slab_buffer,
                           WB_UP(AUDIO_BLOCK_SIZE), AUDIO_NUM_BLOCKS);

struct wav_header {
    char riff[4];
    uint32_t file_size;
    char wave[4];
    char fmt_id[4];
    uint32_t fmt_size;
    uint16_t audio_format;
    uint16_t num_channels;
    uint32_t sample_rate;
    uint32_t byte_rate;
    uint16_t block_align;
    uint16_t bits_per_sample;
    char data_id[4];
    uint32_t data_size;
} __packed;

static const struct device *i2s_dev;
static FATFS fat_fs;
static struct fs_mount_t sd_mount = {
    .type = FS_FATFS,
    .fs_data = &fat_fs,
    .mnt_point = AUDIO_SD_MOUNT_POINT,
};
static bool sd_mounted;
static uint8_t volume_percent = 100U;

static int probe_sd_disk(void)
{
    uint8_t sector0[64];
    uint32_t sector_count = 0U;
    uint32_t sector_size = 0U;
    int ret;

    ret = disk_access_ioctl(AUDIO_SD_DISK_NAME, DISK_IOCTL_CTRL_INIT, NULL);
    printk("Audio SD disk init: %d\n", ret);
    if (ret == -ENOTSUP) {
        printk("Audio SD disk init ioctl not supported; continuing probe\n");
    } else if (ret < 0) {
        return ret;
    }

    ret = disk_access_status(AUDIO_SD_DISK_NAME);
    printk("Audio SD disk status: %d\n", ret);
    if (ret < 0) {
        return ret;
    }

    ret = disk_access_ioctl(AUDIO_SD_DISK_NAME, DISK_IOCTL_GET_SECTOR_COUNT,
                            &sector_count);
    printk("Audio SD sector count: ret=%d count=%u\n", ret, sector_count);
    if (ret < 0) {
        return ret;
    }

    ret = disk_access_ioctl(AUDIO_SD_DISK_NAME, DISK_IOCTL_GET_SECTOR_SIZE,
                            &sector_size);
    printk("Audio SD sector size: ret=%d size=%u\n", ret, sector_size);
    if (ret < 0) {
        return ret;
    }

    memset(sector0, 0, sizeof(sector0));
    ret = disk_access_read(AUDIO_SD_DISK_NAME, sector0, 0, 1);
    printk("Audio SD sector0 read: %d\n", ret);
    if (ret < 0) {
        return ret;
    }

    printk("Audio SD sector0[0..15]: "
           "%02x %02x %02x %02x %02x %02x %02x %02x "
           "%02x %02x %02x %02x %02x %02x %02x %02x\n",
           sector0[0], sector0[1], sector0[2], sector0[3],
           sector0[4], sector0[5], sector0[6], sector0[7],
           sector0[8], sector0[9], sector0[10], sector0[11],
           sector0[12], sector0[13], sector0[14], sector0[15]);

    return 0;
}

static void print_wav_header(const struct wav_header *hdr)
{
    printk("WAV: %u Hz, %u channel(s), %u-bit, %u data bytes\n",
           hdr->sample_rate, hdr->num_channels, hdr->bits_per_sample,
           hdr->data_size);
}

static int mount_sd(void)
{
    int ret;

    if (sd_mounted) {
        return 0;
    }

#if AUDIO_SD_VERBOSE_DISK_PROBE
    ret = probe_sd_disk();
    if (ret < 0) {
        printk("Audio SD disk probe failed: %d\n", ret);
        return ret;
    }
#endif

    ret = fs_mount(&sd_mount);
    if (ret < 0) {
        printk("Audio SD mount failed: %d\n", ret);
        return ret;
    }

    sd_mounted = true;
    printk("Audio SD mounted at %s\n", sd_mount.mnt_point);
    return 0;
}

static int read_wav_header(struct fs_file_t *file, struct wav_header *hdr)
{
    ssize_t n = fs_read(file, hdr, sizeof(*hdr));

    if (n < 0) {
        printk("Audio WAV header read failed: %d\n", (int)n);
        return (int)n;
    }

    if (n != sizeof(*hdr)) {
        printk("Audio WAV header short read: %d/%u\n",
               (int)n, (unsigned int)sizeof(*hdr));
        return -EIO;
    }

    print_wav_header(hdr);

    if (memcmp(hdr->riff, "RIFF", 4) != 0 ||
        memcmp(hdr->wave, "WAVE", 4) != 0 ||
        memcmp(hdr->fmt_id, "fmt ", 4) != 0 ||
        memcmp(hdr->data_id, "data", 4) != 0) {
        printk("Audio WAV layout unsupported\n");
        return -EINVAL;
    }

    if (hdr->audio_format != 1U) {
        printk("Audio WAV must be PCM\n");
        return -EINVAL;
    }

    if (hdr->bits_per_sample != 16U) {
        printk("Audio WAV must be 16-bit PCM\n");
        return -EINVAL;
    }

    if (hdr->num_channels != 1U && hdr->num_channels != 2U) {
        printk("Audio WAV must be mono or stereo\n");
        return -EINVAL;
    }

    return 0;
}

static void apply_volume(int16_t *samples, size_t sample_count)
{
    for (size_t i = 0; i < sample_count; i++) {
        samples[i] = (int16_t)(((int32_t)samples[i] * volume_percent) / 100);
    }
}

static ssize_t fill_tx_block_from_wav(struct fs_file_t *file, void *tx_block,
                                      size_t block_size,
                                      const struct wav_header *hdr)
{
    if (hdr->num_channels == 2U) {
        ssize_t n = fs_read(file, tx_block, block_size);

        if (n > 0) {
            apply_volume((int16_t *)tx_block, (size_t)n / sizeof(int16_t));
            if (n < (ssize_t)block_size) {
                memset((uint8_t *)tx_block + n, 0, block_size - (size_t)n);
            }
        }

        return n;
    }

    int16_t *out = (int16_t *)tx_block;
    size_t stereo_frames = block_size / (2U * sizeof(int16_t));
    size_t mono_bytes = stereo_frames * sizeof(int16_t);
    int16_t mono_buf[AUDIO_BLOCK_SIZE / (2U * sizeof(int16_t))];
    ssize_t n = fs_read(file, mono_buf, mono_bytes);

    if (n <= 0) {
        return n;
    }

    size_t samples_read = (size_t)n / sizeof(int16_t);

    for (size_t i = 0; i < samples_read; i++) {
        int16_t scaled = (int16_t)(((int32_t)mono_buf[i] * volume_percent) / 100);

        out[2U * i] = scaled;
        out[(2U * i) + 1U] = scaled;
    }

    for (size_t i = samples_read; i < stereo_frames; i++) {
        out[2U * i] = 0;
        out[(2U * i) + 1U] = 0;
    }

    return n;
}

static int configure_i2s(const struct wav_header *hdr)
{
    struct i2s_config cfg = {
        .word_size = 16U,
        .channels = 2U,
        .format = I2S_FMT_DATA_FORMAT_I2S,
        .frame_clk_freq = hdr->sample_rate,
        .block_size = AUDIO_BLOCK_SIZE,
        .timeout = 2000,
        .options = I2S_OPT_FRAME_CLK_MASTER | I2S_OPT_BIT_CLK_MASTER,
        .mem_slab = &audio_tx_mem_slab,
    };

    return i2s_configure(i2s_dev, I2S_DIR_TX, &cfg);
}

int audio_playback_init(void)
{
    i2s_dev = DEVICE_DT_GET(DT_ALIAS(i2s_tx));
    if (!device_is_ready(i2s_dev)) {
        printk("Audio I2S device is not ready\n");
        return -ENODEV;
    }

    printk("Audio playback ready on %s\n", i2s_dev->name);
    return 0;
}

int audio_playback_play_file(const char *path)
{
    struct fs_file_t file;
    struct wav_header hdr;
    char wav_path[AUDIO_PLAYBACK_MAX_PATH];
    size_t bytes_left;
    bool started = false;
    int ret;

    if (!path || path[0] == '\0') {
        return -EINVAL;
    }

    ret = mount_sd();
    if (ret < 0) {
        return ret;
    }

    if (path[0] == '/') {
        snprintk(wav_path, sizeof(wav_path), "%s", path);
    } else {
        snprintk(wav_path, sizeof(wav_path), "%s/%s", AUDIO_SD_MOUNT_POINT, path);
    }

    printk("Audio play: %s\n", wav_path);
    fs_file_t_init(&file);

    ret = fs_open(&file, wav_path, FS_O_READ);
    if (ret < 0) {
        printk("Audio open failed: %d\n", ret);
        return ret;
    }

    ret = read_wav_header(&file, &hdr);
    if (ret < 0) {
        fs_close(&file);
        return ret;
    }

    ret = configure_i2s(&hdr);
    if (ret < 0) {
        printk("Audio I2S configure failed: %d\n", ret);
        fs_close(&file);
        return ret;
    }

    bytes_left = hdr.data_size;

    while (bytes_left > 0U) {
        void *tx_block;
        ssize_t n;

        ret = k_mem_slab_alloc(&audio_tx_mem_slab, &tx_block, K_FOREVER);
        if (ret < 0) {
            printk("Audio TX block alloc failed: %d\n", ret);
            break;
        }

        n = fill_tx_block_from_wav(&file, tx_block, AUDIO_BLOCK_SIZE, &hdr);
        if (n <= 0) {
            printk("Audio read/conversion failed: %d\n", (int)n);
            k_mem_slab_free(&audio_tx_mem_slab, tx_block);
            ret = n == 0 ? -EIO : (int)n;
            break;
        }

        ret = i2s_write(i2s_dev, tx_block, AUDIO_BLOCK_SIZE);
        if (ret < 0) {
            printk("Audio I2S write failed: %d\n", ret);
            k_mem_slab_free(&audio_tx_mem_slab, tx_block);
            break;
        }

        if (!started) {
            ret = i2s_trigger(i2s_dev, I2S_DIR_TX, I2S_TRIGGER_START);
            if (ret < 0) {
                printk("Audio I2S start failed: %d\n", ret);
                break;
            }
            started = true;
        }

        bytes_left -= MIN(bytes_left, (size_t)n);
    }

    if (started) {
        int drain_ret = i2s_trigger(i2s_dev, I2S_DIR_TX, I2S_TRIGGER_DRAIN);

        if (drain_ret < 0) {
            printk("Audio I2S drain failed: %d\n", drain_ret);
            if (ret == 0) {
                ret = drain_ret;
            }
        }
    }

    fs_close(&file);
    return ret;
}

int audio_playback_stop(void)
{
    int ret = i2s_trigger(i2s_dev, I2S_DIR_TX, I2S_TRIGGER_DROP);

    if (ret < 0) {
        printk("Audio stop failed: %d\n", ret);
        return ret;
    }

    printk("Audio stopped\n");
    return 0;
}

int audio_playback_pause(void)
{
    return -ENOTSUP;
}

int audio_playback_resume(void)
{
    return -ENOTSUP;
}

int audio_playback_set_volume(uint8_t percent)
{
    volume_percent = MIN(percent, 100U);
    printk("Audio volume: %u%%\n", volume_percent);
    return 0;
}
