#include "evrt_app.h"

#include <stdint.h>
#include <zephyr/kernel.h>

#include "ads1292r.h"
#include "audio_playback.h"

#define SAMPLE_PRINT_INTERVAL 25U
#define AUDIO_TEST_FILE "INST.WAV"

int evrt_app_run(void)
{
    printk("eVRT combined bring-up: audio playback followed by ADS1292R test signal\n");

    int ret = audio_playback_init();
    if (ret) {
        printk("Audio initialization failed: %d\n", ret);
        return ret;
    }

    ret = audio_playback_set_volume(100);
    if (ret) {
        printk("Audio volume setup failed: %d\n", ret);
        return ret;
    }

    printk("Audio playback starting: %s\n", AUDIO_TEST_FILE);
    ret = audio_playback_play_file(AUDIO_TEST_FILE);
    if (ret) {
        printk("Audio playback failed: %d\n", ret);
        return ret;
    }
    printk("Audio playback complete; starting ADS1292R internal test signal\n");

    printk("ADS1292R SPI transport: mode 1, 1 MHz\n");

    ret = ads1292r_init();
    if (ret) {
        printk("ADS1292R initialization failed: %d\n", ret);
        return ret;
    }

    uint32_t sample_count = 0;

    while (1) {
        struct ads1292r_sample sample;

        ret = ads1292r_wait_for_sample(K_MSEC(1500));
        if (ret) {
            printk("ADS1292R DRDY timeout: %d\n", ret);
            continue;
        }

        ret = ads1292r_read_sample(&sample);
        if (ret) {
            printk("ADS1292R sample read failed: %d\n", ret);
            continue;
        }

        sample_count++;
        if ((sample_count % SAMPLE_PRINT_INTERVAL) == 0U) {
            printk("ADS sample %lu: status=%06x ch1=%ld ch2=%ld raw=%02x %02x %02x %02x %02x %02x %02x %02x %02x\n",
                   (unsigned long)sample_count, sample.status,
                   (long)sample.ch1, (long)sample.ch2,
                   sample.raw[0], sample.raw[1], sample.raw[2], sample.raw[3], sample.raw[4],
                   sample.raw[5], sample.raw[6], sample.raw[7], sample.raw[8]);
        }
    }
}
