#include "gateway_state.h"
#include "FreeRTOS.h"
#include "semphr.h"

#define TEMP_ALARM_X10 300u   /* 30.0 度 */

static gateway_state_t   g_state;
static SemaphoreHandle_t g_lock;

/* 温度可能来自 DS18B20 或 Modbus, 告警统一按"当前生效的温度"算 */
static void refresh_alarm(void)
{
    g_state.alarm = (gw_temp_x10(&g_state) >= TEMP_ALARM_X10);
}

/* 创建互斥锁。必须在其他任务使用它之前调用 */
void gateway_state_init(void)
{
    g_lock = xSemaphoreCreateMutex();
}

/* 通信成功才更新数值; 失败了只把 ok 清 0, 保留上一次的有效值 */
void gateway_state_set_modbus(const uint16_t *regs, uint8_t ok)
{
    xSemaphoreTake(g_lock, portMAX_DELAY);

    if (ok)
    {
        g_state.modbus_temp_x10     = regs[0];
        g_state.modbus_humidity_x10 = regs[1];
        g_state.modbus_status       = regs[2];
        refresh_alarm();
    }
    g_state.modbus_ok = ok;

    xSemaphoreGive(g_lock);
}

void gateway_state_set_temp_local(int16_t x10, uint8_t ok)
{
    xSemaphoreTake(g_lock, portMAX_DELAY);

    if (ok)
    {
        g_state.local_temp_x10 = x10;
    }
    g_state.ds18b20_ok = ok;
    refresh_alarm();

    xSemaphoreGive(g_lock);
}

void gateway_state_set_vib(uint16_t mg, uint8_t ok)
{
    xSemaphoreTake(g_lock, portMAX_DELAY);

    if (ok)
    {
        g_state.vibration_mg = mg;
    }
    g_state.mpu_ok = ok;

    xSemaphoreGive(g_lock);
}

/* 一次性拷一份出来用, 拿到手就是"那一刻的完整快照", 后面随便用 */
void gateway_state_snapshot(gateway_state_t *out)
{
    xSemaphoreTake(g_lock, portMAX_DELAY);
    *out = g_state;
    xSemaphoreGive(g_lock);
}
