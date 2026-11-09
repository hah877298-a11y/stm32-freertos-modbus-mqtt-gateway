#ifndef __MODBUS_RTU_H
#define __MODBUS_RTU_H

#include <stdint.h>
void modbus_rtu_init(void);
uint16_t modbus_crc16(const uint8_t *data, uint16_t len);
int modbus_read_regs(uint8_t slave, uint16_t reg, uint16_t count, uint16_t *out);


#endif

