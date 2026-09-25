#ifndef ADS1292R_H
#define ADS1292R_H

#include <stddef.h>
#include <stdint.h>
#include <zephyr/kernel.h>

#define ADS1292R_REG_ID 0x00
#define ADS1292R_REG_CONFIG1 0x01
#define ADS1292R_REG_CONFIG2 0x02
#define ADS1292R_REG_LOFF 0x03
#define ADS1292R_REG_CH1SET 0x04
#define ADS1292R_REG_CH2SET 0x05
#define ADS1292R_REG_RLDSENS 0x06
#define ADS1292R_REG_LOFFSENS 0x07
#define ADS1292R_REG_LOFFSTAT 0x08
#define ADS1292R_REG_RESP1 0x09
#define ADS1292R_REG_RESP2 0x0a
#define ADS1292R_REG_GPIO 0x0b
#define ADS1292R_REGISTER_COUNT 12
#define ADS1292R_EXPECTED_ID 0x73
#define ADS1292R_FRAME_SIZE 9

struct ads1292r_sample {
    uint32_t status;
    int32_t ch1;
    int32_t ch2;
    uint8_t raw[ADS1292R_FRAME_SIZE];
};

int ads1292r_init(void);
int ads1292r_start(void);
int ads1292r_stop(void);
int ads1292r_read_registers(uint8_t address, uint8_t *values, size_t count);
int ads1292r_write_registers(uint8_t address, const uint8_t *values, size_t count);
int ads1292r_wait_for_sample(k_timeout_t timeout);
int ads1292r_read_sample(struct ads1292r_sample *sample);

#endif
