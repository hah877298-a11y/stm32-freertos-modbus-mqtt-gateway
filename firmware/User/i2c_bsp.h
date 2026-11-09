#ifndef __I2C_BSP_H
#define __I2C_BSP_H

#include <stdint.h>

void    i2c_bsp_init(void);
uint8_t i2c_scan(uint8_t *out, uint8_t max);

/* 返回 0 = 成功, 1 = 从机没应答 */
uint8_t i2c_write_reg(uint8_t dev, uint8_t reg, uint8_t val);
uint8_t i2c_read_regs(uint8_t dev, uint8_t reg, uint8_t *buf, uint8_t len);

#endif
