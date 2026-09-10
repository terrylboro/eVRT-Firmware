#ifndef ADS1292R_H
#define ADS1292R_H

#include <stddef.h>
#include <stdint.h>

#define ADS1292R_REG_ID 0x00
#define ADS1292R_REG_CONFIG1 0x01
#define ADS1292R_REGISTER_COUNT 12
#define ADS1292R_EXPECTED_ID 0x73

/* Thread-context only. init mirrors ProtoCentral startup and leaves RDATAC disabled.
 * Register writes require datasheet-valid reserved bits supplied by caller.
 */
int ads1292r_spi_smoke_test(void);
int ads1292r_init(void);
int ads1292r_read_registers(uint8_t address, uint8_t *values, size_t count);
int ads1292r_write_registers(uint8_t address, const uint8_t *values, size_t count);

#endif


