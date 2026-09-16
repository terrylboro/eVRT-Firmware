#include "bhi360_zephyr.h"

#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <zephyr/device.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>

#include "bhi360.h"
#include "bhi360_event_data.h"
#include "bhi360_parse.h"
#include "bhi360_virtual_sensor_conf_param.h"

#if defined(BHI360_ZEPHYR_USE_BMM350_FW)
#include "Bosch_Shuttle3_BHI360_BMM350_bsxsam_ndof.fw.h"
#define BHI360_FIRMWARE_NAME "BHI360_BMM350_bsxsam_ndof"
#else
#include "Bosch_Shuttle3_BHI360_bsxsam.fw.h"
#define BHI360_FIRMWARE_NAME "BHI360_bsxsam"
#endif

#define BHI360_NODE DT_NODELABEL(bhi360)
#define BHI360_READ_WRITE_LEN 256U
#define BHI360_FIRMWARE_CHUNK_LEN 256U
#define BHI360_STREAM_WORK_BUFFER_SIZE 2048U
#define BMM350_I2C_ADDR 0x14U
#define BMM350_REG_CHIP_ID 0x40U
#define BMM350_CHIP_ID 0x32U

static const struct i2c_dt_spec bhi360_i2c = I2C_DT_SPEC_GET(BHI360_NODE);
static uint8_t bhi360_i2c_addr;
static struct bhi360_dev bhi360_dev;
static uint8_t stream_work_buffer[BHI360_STREAM_WORK_BUFFER_SIZE];
static struct bhi360_imu_sample latest_sample;
static K_MUTEX_DEFINE(bhi360_lock);

static int probe_i2c_addr(const struct device *bus, uint8_t addr)
{
    struct i2c_msg msg = {
        .buf = NULL,
        .len = 0,
        .flags = I2C_MSG_WRITE | I2C_MSG_STOP,
    };

    return i2c_transfer(bus, &msg, 1, addr);
}

static int read_chip_id_at_addr(uint8_t addr, uint8_t *chip_id)
{
    uint8_t reg_addr = BHI360_REG_CHIP_ID;
    int combined_ret;
    int write_ret;
    int read_ret = -EIO;

    if (!chip_id) {
        return -EINVAL;
    }

    *chip_id = 0U;
    combined_ret = i2c_write_read(bhi360_i2c.bus, addr, &reg_addr, sizeof(reg_addr),
                                  chip_id, 1U);
    if (combined_ret == 0) {
        printk("BHI360 chip-id combined read addr=0x%02x reg=0x%02x: chip_id=0x%02x\n",
               addr, reg_addr, *chip_id);
        return 0;
    }

    write_ret = i2c_write(bhi360_i2c.bus, &reg_addr, sizeof(reg_addr), addr);
    if (write_ret == 0) {
        read_ret = i2c_read(bhi360_i2c.bus, chip_id, 1U, addr);
        if (read_ret == 0) {
            printk("BHI360 chip-id split read addr=0x%02x reg=0x%02x: chip_id=0x%02x\n",
                   addr, reg_addr, *chip_id);
            return 0;
        }
    }

    printk("BHI360 chip-id read failed addr=0x%02x reg=0x%02x combined_ret=%d write_ret=%d read_ret=%d\n",
           addr, reg_addr, combined_ret, write_ret, read_ret);

    return write_ret != 0 ? write_ret : read_ret;
}

static int read_reg_u8_at_addr(uint8_t addr, uint8_t reg_addr, uint8_t *value)
{
    int combined_ret;
    int write_ret;
    int read_ret = -EIO;

    if (!value) {
        return -EINVAL;
    }

    *value = 0U;
    combined_ret = i2c_write_read(bhi360_i2c.bus, addr, &reg_addr, sizeof(reg_addr),
                                  value, 1U);
    if (combined_ret == 0) {
        return 0;
    }

    write_ret = i2c_write(bhi360_i2c.bus, &reg_addr, sizeof(reg_addr), addr);
    if (write_ret == 0) {
        read_ret = i2c_read(bhi360_i2c.bus, value, 1U, addr);
        if (read_ret == 0) {
            return 0;
        }
    }

    printk("I2C reg read failed addr=0x%02x reg=0x%02x combined_ret=%d write_ret=%d read_ret=%d\n",
           addr, reg_addr, combined_ret, write_ret, read_ret);

    return write_ret != 0 ? write_ret : read_ret;
}

static int probe_bmm350_on_host_bus(void)
{
    uint8_t chip_id = 0U;
    int ret;

    ret = read_reg_u8_at_addr(BMM350_I2C_ADDR, BMM350_REG_CHIP_ID, &chip_id);
    printk("BMM350 host-bus chip probe 0x%02x/0x%02x: ret=%d chip_id=0x%02x expected=0x%02x\n",
           BMM350_I2C_ADDR, BMM350_REG_CHIP_ID, ret, chip_id, BMM350_CHIP_ID);

    if (ret == 0 && chip_id == BMM350_CHIP_ID) {
        printk("BMM350 detected on host I2C bus\n");
        return 0;
    }

    printk("BMM350 not detected on host I2C bus; it may be routed behind the BHI360 aux bus\n");

    return ret == 0 ? -ENODEV : ret;
}

static void scan_nearby_i2c_devices(const struct device *bus)
{
    printk("Quick I2C address probe 0x20..0x2f for nearby debug context...\n");

    for (uint8_t addr = 0x20; addr <= 0x2f; addr++) {
        int ret = probe_i2c_addr(bus, addr);

        if (ret == 0) {
            printk("  quick ACK at 0x%02x\n", addr);
        }

        if (ret != 0) {
            (void)i2c_recover_bus(bus);
        }

        k_sleep(K_MSEC(1));
    }

    (void)i2c_recover_bus(bus);
    k_sleep(K_MSEC(10));
}

static int8_t zephyr_i2c_read(uint8_t reg_addr, uint8_t *reg_data, uint32_t length,
                              void *intf_ptr)
{
    const uint8_t *addr = intf_ptr;

    if (!addr || !reg_data || length > UINT16_MAX) {
        return -1;
    }

    int ret = i2c_write_read(bhi360_i2c.bus, *addr, &reg_addr, sizeof(reg_addr),
                             reg_data, (uint16_t)length);

    if (ret) {
        int first_ret = ret;

        ret = i2c_write(bhi360_i2c.bus, &reg_addr, sizeof(reg_addr), *addr);
        if (ret == 0) {
            ret = i2c_read(bhi360_i2c.bus, reg_data, (uint32_t)length, *addr);
        }

        if (ret) {
            printk("BHI360 I2C read failed: addr=0x%02x reg=0x%02x len=%lu combined_ret=%d split_ret=%d\n",
                   *addr, reg_addr, (unsigned long)length, first_ret, ret);
            return -1;
        }

        printk("BHI360 I2C read used split transaction fallback: addr=0x%02x reg=0x%02x len=%lu\n",
               *addr, reg_addr, (unsigned long)length);
    }

    return BHI360_INTF_RET_SUCCESS;
}

static int8_t zephyr_i2c_write(uint8_t reg_addr, const uint8_t *reg_data, uint32_t length,
                               void *intf_ptr)
{
    const uint8_t *addr = intf_ptr;
    uint8_t tx[BHI360_READ_WRITE_LEN + 1U];

    if (!addr || (!reg_data && length > 0U) || length > BHI360_READ_WRITE_LEN) {
        return -1;
    }

    tx[0] = reg_addr;
    if (length > 0U) {
        memcpy(&tx[1], reg_data, length);
    }

    int ret = i2c_write(bhi360_i2c.bus, tx, (uint32_t)(length + 1U), *addr);

    if (ret) {
        printk("BHI360 I2C write failed: addr=0x%02x reg=0x%02x len=%lu ret=%d\n",
               *addr, reg_addr, (unsigned long)length, ret);
        return -1;
    }

    return BHI360_INTF_RET_SUCCESS;
}

static void zephyr_delay_us(uint32_t period_us, void *intf_ptr)
{
    ARG_UNUSED(intf_ptr);

    if (period_us >= 1000U) {
        k_sleep(K_USEC(period_us));
    } else {
        k_busy_wait(period_us);
    }
}

static int upload_firmware_partly(struct bhi360_dev *dev)
{
    uint32_t chunk_len = BHI360_FIRMWARE_CHUNK_LEN;
    uint32_t firmware_len = sizeof(bhi360_firmware_image);
    int8_t rslt = BHI360_OK;

    for (uint32_t pos = 0U; pos < firmware_len && rslt == BHI360_OK; pos += chunk_len) {
        uint32_t this_len = MIN(chunk_len, firmware_len - pos);

        if ((this_len % 4U) != 0U) {
            this_len = ROUND_UP(this_len, 4U);
        }

        rslt = bhi360_upload_firmware_to_ram_partly(&bhi360_firmware_image[pos],
                                                    firmware_len, pos, this_len, dev);
        if ((pos % 4096U) == 0U || pos + chunk_len >= firmware_len) {
            printk("BHI360 firmware upload: %lu/%lu bytes\n",
                   (unsigned long)MIN(pos + chunk_len, firmware_len),
                   (unsigned long)firmware_len);
        }
    }

    return rslt;
}

static int api_result_to_errno(int8_t rslt)
{
    return rslt == BHI360_OK ? 0 : -EIO;
}

static void update_latest_xyz_callback(const struct bhi360_fifo_parse_data_info *callback_info,
                                       void *callback_ref)
{
    struct bhi360_event_data_xyz data;
    int64_t now_ms = k_uptime_get();

    ARG_UNUSED(callback_ref);

    if (!callback_info || !callback_info->data_ptr) {
        return;
    }

    bhi360_event_data_parse_xyz(callback_info->data_ptr, &data);

    switch (callback_info->sensor_id) {
    case BHI360_SENSOR_ID_ACC_PASS:
        latest_sample.ax_g = (float)data.x / 4096.0f;
        latest_sample.ay_g = (float)data.y / 4096.0f;
        latest_sample.az_g = (float)data.z / 4096.0f;
        latest_sample.has_accel = true;
        break;
    case BHI360_SENSOR_ID_GYRO_PASS:
        latest_sample.gx_dps = (float)data.x * 2000.0f / 32768.0f;
        latest_sample.gy_dps = (float)data.y * 2000.0f / 32768.0f;
        latest_sample.gz_dps = (float)data.z * 2000.0f / 32768.0f;
        latest_sample.has_gyro = true;
        break;
    default:
        return;
    }

    latest_sample.timestamp_ms = now_ms;
}

int bhi360_zephyr_start_streaming(float sample_rate_hz)
{
    struct bhi360_virtual_sensor_conf_param_conf sensor_conf = {
        .sample_rate = sample_rate_hz,
        .latency = 0U,
    };
    int8_t rslt;
    int ret = 0;

    k_mutex_lock(&bhi360_lock, K_FOREVER);
    memset(&latest_sample, 0, sizeof(latest_sample));

    rslt = bhi360_register_fifo_parse_callback(BHI360_SYS_ID_META_EVENT,
                                               bhi360_parse_meta_event, NULL, &bhi360_dev);
    if (rslt != BHI360_OK) {
        printk("BHI360 meta-event callback registration failed: %d\n", rslt);
        ret = api_result_to_errno(rslt);
        goto out;
    }

    rslt = bhi360_register_fifo_parse_callback(BHI360_SYS_ID_META_EVENT_WU,
                                               bhi360_parse_meta_event, NULL, &bhi360_dev);
    if (rslt != BHI360_OK) {
        printk("BHI360 wake meta-event callback registration failed: %d\n", rslt);
        ret = api_result_to_errno(rslt);
        goto out;
    }

    rslt = bhi360_register_fifo_parse_callback(BHI360_SENSOR_ID_ACC_PASS,
                                               update_latest_xyz_callback, NULL, &bhi360_dev);
    if (rslt != BHI360_OK) {
        printk("BHI360 accel callback registration failed: %d\n", rslt);
        ret = api_result_to_errno(rslt);
        goto out;
    }

    rslt = bhi360_register_fifo_parse_callback(BHI360_SENSOR_ID_GYRO_PASS,
                                               update_latest_xyz_callback, NULL, &bhi360_dev);
    if (rslt != BHI360_OK) {
        printk("BHI360 gyro callback registration failed: %d\n", rslt);
        ret = api_result_to_errno(rslt);
        goto out;
    }

    rslt = bhi360_get_and_process_fifo(stream_work_buffer, sizeof(stream_work_buffer),
                                       &bhi360_dev);
    if (rslt != BHI360_OK) {
        printk("BHI360 initial FIFO drain failed: %d\n", rslt);
        ret = api_result_to_errno(rslt);
        goto out;
    }

    rslt = bhi360_update_virtual_sensor_list(&bhi360_dev);
    if (rslt != BHI360_OK) {
        printk("BHI360 virtual sensor list update failed: %d\n", rslt);
        ret = api_result_to_errno(rslt);
        goto out;
    }

    rslt = bhi360_virtual_sensor_conf_param_set_cfg(BHI360_SENSOR_ID_ACC_PASS,
                                                    &sensor_conf, &bhi360_dev);
    if (rslt != BHI360_OK) {
        printk("BHI360 accel stream enable failed: %d\n", rslt);
        ret = api_result_to_errno(rslt);
        goto out;
    }

    rslt = bhi360_virtual_sensor_conf_param_set_cfg(BHI360_SENSOR_ID_GYRO_PASS,
                                                    &sensor_conf, &bhi360_dev);
    if (rslt != BHI360_OK) {
        printk("BHI360 gyro stream enable failed: %d\n", rslt);
        ret = api_result_to_errno(rslt);
        goto out;
    }

    printk("BHI360 streaming enabled: accel pass + gyro pass at %u Hz\n",
           (unsigned int)sample_rate_hz);

out:
    k_mutex_unlock(&bhi360_lock);
    return ret;
}

int bhi360_zephyr_process_fifo(void)
{
    int8_t rslt;

    k_mutex_lock(&bhi360_lock, K_FOREVER);
    rslt = bhi360_get_and_process_fifo(stream_work_buffer, sizeof(stream_work_buffer),
                                       &bhi360_dev);
    k_mutex_unlock(&bhi360_lock);

    if (rslt != BHI360_OK) {
        printk("BHI360 FIFO process failed: %d\n", rslt);
        return api_result_to_errno(rslt);
    }

    return 0;
}

int bhi360_zephyr_get_latest(struct bhi360_imu_sample *sample)
{
    if (!sample) {
        return -EINVAL;
    }

    k_mutex_lock(&bhi360_lock, K_FOREVER);
    *sample = latest_sample;
    k_mutex_unlock(&bhi360_lock);

    return sample->has_accel || sample->has_gyro ? 0 : -EAGAIN;
}

int bhi360_zephyr_boot(struct bhi360_zephyr_info *info)
{
    uint8_t hintr_ctrl;
    uint8_t hif_ctrl;
    int8_t rslt;
    int ret = 0;

    if (!info) {
        return -EINVAL;
    }

    memset(info, 0, sizeof(*info));

    if (!device_is_ready(bhi360_i2c.bus)) {
        printk("BHI360 I2C bus is not ready\n");
        return -ENODEV;
    }

    k_mutex_lock(&bhi360_lock, K_FOREVER);

    printk("BHI360 init on %s\n", bhi360_i2c.bus->name);
    k_sleep(K_MSEC(100));

    (void)probe_bmm350_on_host_bus();

    uint8_t candidate_addrs[] = { 0x28, 0x29 };
    bhi360_i2c_addr = 0U;
    for (size_t i = 0; i < ARRAY_SIZE(candidate_addrs); i++) {
        uint8_t chip_id = 0U;
        int chip_ret;

        chip_ret = read_chip_id_at_addr(candidate_addrs[i], &chip_id);
        printk("BHI360 chip probe 0x%02x: ret=%d chip_id=0x%02x\n",
               candidate_addrs[i], chip_ret, chip_id);

        if (chip_ret != 0) {
            (void)i2c_recover_bus(bhi360_i2c.bus);
            k_sleep(K_MSEC(1));
        }

        if (chip_ret == 0 && chip_id == BHI360_CHIP_ID && bhi360_i2c_addr == 0U) {
            bhi360_i2c_addr = candidate_addrs[i];
        }
    }

    if (bhi360_i2c_addr == 0U) {
        printk("No BHI360 chip ID found at 0x28 or 0x29\n");
        scan_nearby_i2c_devices(bhi360_i2c.bus);
        printk("Check VDD, VDDIO, GND, SDA/SCL order, and 1V8 pullups\n");
        ret = -ENODEV;
        goto out;
    }

    printk("BHI360 using I2C address 0x%02x\n", bhi360_i2c_addr);

    rslt = bhi360_init(BHI360_I2C_INTERFACE, zephyr_i2c_read, zephyr_i2c_write,
                       zephyr_delay_us, BHI360_READ_WRITE_LEN, (void *)&bhi360_i2c_addr,
                       &bhi360_dev);
    if (rslt != BHI360_OK) {
        printk("BHI360 init failed: %d\n", rslt);
        ret = api_result_to_errno(rslt);
        goto out;
    }

    rslt = bhi360_get_chip_id(&info->chip_id, &bhi360_dev);
    if (rslt != BHI360_OK || info->chip_id != BHI360_CHIP_ID) {
        printk("BHI360 pre-reset chip ID check failed: rslt=%d chip_id=0x%02x expected=0x%02x\n",
               rslt, info->chip_id, BHI360_CHIP_ID);
        ret = rslt == BHI360_OK ? -ENODEV : api_result_to_errno(rslt);
        goto out;
    }

    (void)bhi360_get_product_id(&info->product_id, &bhi360_dev);
    (void)bhi360_get_revision_id(&info->revision_id, &bhi360_dev);
    (void)bhi360_get_rom_version(&info->rom_version, &bhi360_dev);

    printk("BHI360 pre-reset: chip=0x%02x product=0x%02x revision=0x%02x rom=%u\n",
           info->chip_id, info->product_id, info->revision_id, info->rom_version);

    rslt = bhi360_soft_reset(&bhi360_dev);
    if (rslt != BHI360_OK) {
        printk("BHI360 soft reset failed: %d intf_rslt=%d\n",
               rslt, bhi360_dev.hif.intf_rslt);
        ret = api_result_to_errno(rslt);
        goto out;
    }

    rslt = bhi360_get_chip_id(&info->chip_id, &bhi360_dev);
    if (rslt != BHI360_OK || info->chip_id != BHI360_CHIP_ID) {
        printk("BHI360 chip ID check failed: rslt=%d chip_id=0x%02x expected=0x%02x\n",
               rslt, info->chip_id, BHI360_CHIP_ID);
        ret = rslt == BHI360_OK ? -ENODEV : api_result_to_errno(rslt);
        goto out;
    }

    (void)bhi360_get_product_id(&info->product_id, &bhi360_dev);
    (void)bhi360_get_revision_id(&info->revision_id, &bhi360_dev);
    (void)bhi360_get_rom_version(&info->rom_version, &bhi360_dev);

    printk("BHI360 detected: chip=0x%02x product=0x%02x revision=0x%02x rom=%u\n",
           info->chip_id, info->product_id, info->revision_id, info->rom_version);

    hintr_ctrl = BHI360_ICTL_DISABLE_STATUS_FIFO | BHI360_ICTL_DISABLE_DEBUG;
    rslt = bhi360_set_host_interrupt_ctrl(hintr_ctrl, &bhi360_dev);
    if (rslt != BHI360_OK) {
        printk("BHI360 host interrupt control failed: %d\n", rslt);
        ret = api_result_to_errno(rslt);
        goto out;
    }

    hif_ctrl = 0U;
    rslt = bhi360_set_host_intf_ctrl(hif_ctrl, &bhi360_dev);
    if (rslt != BHI360_OK) {
        printk("BHI360 host interface control failed: %d\n", rslt);
        ret = api_result_to_errno(rslt);
        goto out;
    }

    rslt = bhi360_get_boot_status(&info->boot_status, &bhi360_dev);
    if (rslt != BHI360_OK || !(info->boot_status & BHI360_BST_HOST_INTERFACE_READY)) {
        printk("BHI360 host interface not ready: rslt=%d boot_status=0x%02x\n",
               rslt, info->boot_status);
        ret = rslt == BHI360_OK ? -EIO : api_result_to_errno(rslt);
        goto out;
    }

    printk("BHI360 loading %s firmware (%lu bytes)\n",
           BHI360_FIRMWARE_NAME, (unsigned long)sizeof(bhi360_firmware_image));

    ret = api_result_to_errno(upload_firmware_partly(&bhi360_dev));
    (void)bhi360_get_error_value(&info->sensor_error, &bhi360_dev);
    if (ret || info->sensor_error) {
        printk("BHI360 firmware upload failed: ret=%d sensor_error=0x%02x\n",
               ret, info->sensor_error);
        goto out;
    }

    printk("BHI360 booting from RAM\n");
    rslt = bhi360_boot_from_ram(&bhi360_dev);
    (void)bhi360_get_error_value(&info->sensor_error, &bhi360_dev);
    if (rslt != BHI360_OK || info->sensor_error) {
        printk("BHI360 RAM boot reported: rslt=%d sensor_error=0x%02x\n",
               rslt, info->sensor_error);
        if (info->sensor_error == BHI360_ERR_UNEXPECTED_DEVICE_ID) {
            printk("BHI360 sensor error 0x21 means sensor init failed: unexpected device ID\n");
        }

        if (rslt != BHI360_OK) {
            ret = api_result_to_errno(rslt);
            goto out;
        }
    }

    rslt = bhi360_get_kernel_version(&info->kernel_version, &bhi360_dev);
    if (rslt != BHI360_OK) {
        printk("BHI360 kernel version read failed: %d\n", rslt);
        ret = api_result_to_errno(rslt);
        goto out;
    }

    printk("BHI360 boot complete: kernel=%u\n", info->kernel_version);

out:
    k_mutex_unlock(&bhi360_lock);
    return ret;
}
