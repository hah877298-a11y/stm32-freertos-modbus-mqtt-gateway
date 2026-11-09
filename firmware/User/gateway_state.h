#ifndef __GATEWAY_STATE_H
#define __GATEWAY_STATE_H

#include <stdint.h>

/* 整个网关"当前的最新状态"。所有任务都只通过下面几个函数碰它,
   谁都不许直接读写 g_state, 这样才不会出现"读到一半被改掉"的问题。 */
typedef struct
{
    uint16_t modbus_temp_x10;       /* Modbus 温度, 250 = 25.0 度 */
    uint16_t modbus_humidity_x10;   /* Modbus 湿度, 500 = 50.0 % */
    uint16_t modbus_status;         /* Modbus 状态寄存器 */
    uint8_t  modbus_ok;             /* 1 = 最近一次通信成功 */
    int16_t  local_temp_x10;        /* DS18B20 温度, 256 = 25.6 度 */
    uint8_t  ds18b20_ok;            /* 1 = 本地传感器读到有效温度 */
    uint16_t vibration_mg;          /* 振动强度 mg */
    uint8_t  mpu_ok;                /* 1 = 最近一次读到加速度 */
    uint8_t  alarm;                 /* 1 = 温度超过阈值 */
} gateway_state_t;

void gateway_state_init(void);
void gateway_state_set_modbus(const uint16_t *regs, uint8_t ok);
void gateway_state_set_temp_local(int16_t x10, uint8_t ok);
void gateway_state_set_vib(uint16_t mg, uint8_t ok);

void gateway_state_snapshot(gateway_state_t *out);
/* 温度优先用本地传感器, 没有就退回 Modbus 的值 */
static inline uint16_t gw_temp_x10(const gateway_state_t *s)
{
    if (s->ds18b20_ok && s->local_temp_x10 >= 0)
    {
        return (uint16_t)s->local_temp_x10;
    }
    if (s->modbus_ok)
    {
        return s->modbus_temp_x10;
    }
    /* 本地偶发读失败时沿用上一次有效温度, 避免云端出现 0.0 */
    if (s->local_temp_x10 >= 0)
    {
        return (uint16_t)s->local_temp_x10;
    }
    return s->modbus_temp_x10;
}

#endif
