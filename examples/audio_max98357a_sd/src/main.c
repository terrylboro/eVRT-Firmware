#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include "audio_playback.h"

#define AUDIO_TEST_FILE "INSTRU~1.WAV"

int main(void)
{
    int ret;

    printk("MAX98357A SD WAV playback baseline\n");

    ret = audio_playback_init();
    if (ret) {
        printk("Audio init failed: %d\n", ret);
        return ret;
    }

    ret = audio_playback_set_volume(100);
    if (ret) {
        printk("Audio volume setup failed: %d\n", ret);
        return ret;
    }

    ret = audio_playback_play_file(AUDIO_TEST_FILE);
    if (ret) {
        printk("Audio playback failed: %d\n", ret);
        return ret;
    }

    printk("Audio playback complete\n");
    return 0;
}
