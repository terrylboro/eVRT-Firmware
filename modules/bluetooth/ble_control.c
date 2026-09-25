#include "ble_control.h"

#include <stdlib.h>
#include <string.h>

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>

#include "app_cmd.h"

#define BT_UUID_EVRT_SERVICE_VAL \
    BT_UUID_128_ENCODE(0x12345678, 0x1234, 0x5678, 0x1234, 0x56789abcdef0)

#define BT_UUID_EVRT_CTRL_VAL \
    BT_UUID_128_ENCODE(0x12345678, 0x1234, 0x5678, 0x1234, 0x56789abcdef1)

static struct k_msgq *g_cmd_q;

static struct bt_uuid_128 evrt_service_uuid =
    BT_UUID_INIT_128(BT_UUID_EVRT_SERVICE_VAL);

static struct bt_uuid_128 evrt_ctrl_uuid =
    BT_UUID_INIT_128(BT_UUID_EVRT_CTRL_VAL);

static ssize_t ctrl_write(struct bt_conn *conn,
                          const struct bt_gatt_attr *attr,
                          const void *buf,
                          uint16_t len,
                          uint16_t offset,
                          uint8_t flags)
{
    ARG_UNUSED(conn);
    ARG_UNUSED(attr);
    ARG_UNUSED(flags);

    struct app_cmd cmd = {0};
    char tmp[48];
    size_t n = MIN((size_t)len, sizeof(tmp) - 1);

    if (offset != 0) {
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
    }

    memcpy(tmp, buf, n);
    tmp[n] = '\0';

    printk("BLE cmd: %s\n", tmp);

    if (strcmp(tmp, "STOP") == 0) {
        cmd.type = APP_CMD_STOP;
    } else if (strcmp(tmp, "PAUSE") == 0) {
        cmd.type = APP_CMD_PAUSE;
    } else if (strcmp(tmp, "RESUME") == 0) {
        cmd.type = APP_CMD_RESUME;
    } else if (strcmp(tmp, "ADS_START") == 0) {
        cmd.type = APP_CMD_ADS_START;
    } else if (strcmp(tmp, "ADS_STOP") == 0) {
        cmd.type = APP_CMD_ADS_STOP;
    } else if (strncmp(tmp, "VOL:", 4) == 0) {
        cmd.type = APP_CMD_SET_VOLUME;
        cmd.volume = (uint8_t)atoi(&tmp[4]);
    } else if (strncmp(tmp, "PLAY:", 5) == 0) {
        cmd.type = APP_CMD_PLAY;
        strncpy(cmd.filename, &tmp[5], sizeof(cmd.filename) - 1);
        cmd.filename[sizeof(cmd.filename) - 1] = '\0';
    } else {
        printk("Unknown BLE command\n");
        return BT_GATT_ERR(BT_ATT_ERR_VALUE_NOT_ALLOWED);
    }

    if (g_cmd_q == NULL) {
        return BT_GATT_ERR(BT_ATT_ERR_UNLIKELY);
    }

    if (k_msgq_put(g_cmd_q, &cmd, K_NO_WAIT) != 0) {
        printk("BLE command queue full\n");
        return BT_GATT_ERR(BT_ATT_ERR_INSUFFICIENT_RESOURCES);
    }

    return len;
}

BT_GATT_SERVICE_DEFINE(evrt_svc,
    BT_GATT_PRIMARY_SERVICE(&evrt_service_uuid),
    BT_GATT_CHARACTERISTIC(&evrt_ctrl_uuid.uuid,
                           BT_GATT_CHRC_WRITE | BT_GATT_CHRC_WRITE_WITHOUT_RESP,
                           BT_GATT_PERM_WRITE,
                           NULL, ctrl_write, NULL),
);

static const struct bt_data ad[] = {
    BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
    BT_DATA_BYTES(BT_DATA_UUID128_ALL, BT_UUID_EVRT_SERVICE_VAL),
};

static const struct bt_data sd[] = {
    BT_DATA(BT_DATA_NAME_COMPLETE, CONFIG_BT_DEVICE_NAME,
            sizeof(CONFIG_BT_DEVICE_NAME) - 1),
};

static int start_advertising(void)
{
    int err = bt_le_adv_start(BT_LE_ADV_CONN, ad, ARRAY_SIZE(ad),
                              sd, ARRAY_SIZE(sd));

    if (err) {
        printk("BLE advertising failed: %d\n", err);
        return err;
    }

    printk("BLE advertising started as %s\n", CONFIG_BT_DEVICE_NAME);
    return 0;
}

int ble_control_init(struct k_msgq *cmd_q)
{
    int err;

    g_cmd_q = cmd_q;

    err = bt_enable(NULL);
    if (err) {
        printk("Bluetooth init failed: %d\n", err);
        return err;
    }

    printk("Bluetooth initialized\n");
    return start_advertising();
}
