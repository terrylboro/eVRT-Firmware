#include <zephyr/kernel.h>

#include "evrt_app.h"

int main(void)
{
    printk("eVRT firmware build: audio-then-ads-test-flow v1\n");
    (void)evrt_app_run();
    return 0;
}
