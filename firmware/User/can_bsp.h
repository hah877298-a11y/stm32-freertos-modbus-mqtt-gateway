#ifndef __CAN_BSP_H
#define __CAN_BSP_H

#include <stdint.h>

/* 返回 0 = 成功, 非 0 = 失败 */
uint8_t can_bsp_init(void);
uint8_t can_bsp_send(uint32_t id, const uint8_t *data, uint8_t len);
uint8_t can_bsp_recv(uint32_t *id, uint8_t *data, uint8_t len, uint32_t timeout_ms);

#endif
