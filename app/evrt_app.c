#include "evrt_app.h"

#include <stdbool.h>
#include <stdint.h>
#include <zephyr/kernel.h>

#include "ads1292r.h"
#include "app_cmd.h"
#include "audio_playback.h"
#include "ble_control.h"

#define SAMPLE_PRINT_INTERVAL 25U
#define AUDIO_TEST_FILE "INST.WAV"
#define ADS_PRINT_DURATION K_SECONDS(20)
#define CMD_QUEUE_DEPTH 8U
#define ADS_THREAD_STACK_SIZE 2048
#define ADS_THREAD_PRIORITY 6

K_MSGQ_DEFINE(app_cmd_q, sizeof(struct app_cmd), CMD_QUEUE_DEPTH, 4);
K_SEM_DEFINE(ads_print_start_sem, 0, 1);
K_THREAD_STACK_DEFINE(ads_thread_stack, ADS_THREAD_STACK_SIZE);

static struct k_thread ads_thread_data;
static bool ads_printing;
static uint32_t ads_sample_count;

static void print_ads_sample(const struct ads1292r_sample *sample,
                             uint32_t sample_count)
{
    printk("ADS sample %lu: status=%06x ch1=%ld ch2=%ld raw=%02x %02x %02x %02x %02x %02x %02x %02x %02x\n",
           (unsigned long)sample_count, sample->status,
           (long)sample->ch1, (long)sample->ch2,
           sample->raw[0], sample->raw[1], sample->raw[2], sample->raw[3],
           sample->raw[4], sample->raw[5], sample->raw[6], sample->raw[7],
           sample->raw[8]);
}

static int read_ads_sample_and_maybe_print(void)
{
    struct ads1292r_sample sample;
    int ret;

    ret = ads1292r_wait_for_sample(K_MSEC(1500));
    if (ret) {
        printk("ADS1292R DRDY timeout: %d\n", ret);
        return ret;
    }

    ret = ads1292r_read_sample(&sample);
    if (ret) {
        printk("ADS1292R sample read failed: %d\n", ret);
        return ret;
    }

    ads_sample_count++;
    if ((ads_sample_count % SAMPLE_PRINT_INTERVAL) == 0U) {
        print_ads_sample(&sample, ads_sample_count);
    }

    return 0;
}

static void ads_print_thread(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    while (1) {
        k_sem_take(&ads_print_start_sem, K_FOREVER);
        printk("ADS print thread active\n");

        while (ads_printing) {
            (void)read_ads_sample_and_maybe_print();
        }

        printk("ADS print thread idle; samples_read=%lu\n",
               (unsigned long)ads_sample_count);
    }
}

static void handle_cmd(const struct app_cmd *cmd)
{
    int ret;

    switch (cmd->type) {
    case APP_CMD_PLAY:
        printk("Command PLAY: %s\n", cmd->filename);
        ret = audio_playback_play_file(cmd->filename);
        if (ret) {
            printk("Command PLAY failed: %d\n", ret);
        }
        break;
    case APP_CMD_STOP:
        printk("Command STOP\n");
        ret = audio_playback_stop();
        if (ret) {
            printk("Command STOP failed: %d\n", ret);
        }
        break;
    case APP_CMD_PAUSE:
        printk("Command PAUSE\n");
        ret = audio_playback_pause();
        if (ret) {
            printk("Command PAUSE failed: %d\n", ret);
        }
        break;
    case APP_CMD_RESUME:
        printk("Command RESUME\n");
        ret = audio_playback_resume();
        if (ret) {
            printk("Command RESUME failed: %d\n", ret);
        }
        break;
    case APP_CMD_SET_VOLUME:
        printk("Command VOL: %u\n", cmd->volume);
        ret = audio_playback_set_volume(cmd->volume);
        if (ret) {
            printk("Command VOL failed: %d\n", ret);
        }
        break;
    case APP_CMD_ADS_START:
        printk("Command ADS_START\n");
        ret = ads1292r_start();
        if (ret) {
            printk("Command ADS_START failed: %d\n", ret);
        } else if (!ads_printing) {
            ads_printing = true;
            k_sem_give(&ads_print_start_sem);
        }
        break;
    case APP_CMD_ADS_STOP:
        printk("Command ADS_STOP\n");
        ads_printing = false;
        ret = ads1292r_stop();
        if (ret) {
            printk("Command ADS_STOP failed: %d\n", ret);
        }
        break;
    default:
        printk("Unhandled command type: %d\n", cmd->type);
        break;
    }
}

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

    ret = ble_control_init(&app_cmd_q);
    if (ret) {
        printk("BLE control initialization failed: %d\n", ret);
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

    k_timepoint_t ads_print_deadline = sys_timepoint_calc(ADS_PRINT_DURATION);

    printk("ADS printing enabled for 20 seconds\n");

    while (!sys_timepoint_expired(ads_print_deadline)) {
        (void)read_ads_sample_and_maybe_print();
    }

    ret = ads1292r_stop();
    if (ret) {
        printk("ADS deliberate stop requested after 20 seconds, but stop failed: %d\n", ret);
        return ret;
    }

    printk("ADS printing deliberately stopped after 20 seconds; samples_read=%lu\n",
           (unsigned long)ads_sample_count);

    k_thread_create(&ads_thread_data,
                    ads_thread_stack,
                    K_THREAD_STACK_SIZEOF(ads_thread_stack),
                    ads_print_thread,
                    NULL, NULL, NULL,
                    ADS_THREAD_PRIORITY,
                    0,
                    K_NO_WAIT);

    printk("BLE control ready. Commands: PLAY:<file>, VOL:<0-100>, ADS_START, ADS_STOP\n");
    while (1) {
        struct app_cmd cmd;

        if (k_msgq_get(&app_cmd_q, &cmd, K_FOREVER) == 0) {
            handle_cmd(&cmd);
        }
    }
}
