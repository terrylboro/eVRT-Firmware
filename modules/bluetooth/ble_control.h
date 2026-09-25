#ifndef BLE_CONTROL_H
#define BLE_CONTROL_H

#include <zephyr/kernel.h>

int ble_control_init(struct k_msgq *cmd_q);

#endif
