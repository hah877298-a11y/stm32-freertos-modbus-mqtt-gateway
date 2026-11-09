#include "modbus_rtu.h"
#include "usart.h"
#include "stm32f1xx_hal.h"

static UART_HandleTypeDef g_uart3_handle;

void modbus_rtu_init(void)
{
    g_uart3_handle.Instance        = USART3;
    g_uart3_handle.Init.BaudRate   = 9600;
    g_uart3_handle.Init.WordLength = UART_WORDLENGTH_8B;
    g_uart3_handle.Init.StopBits   = UART_STOPBITS_1;
    g_uart3_handle.Init.Parity     = UART_PARITY_NONE;
    g_uart3_handle.Init.HwFlowCtl  = UART_HWCONTROL_NONE;
    g_uart3_handle.Init.Mode       = UART_MODE_TX_RX;
    HAL_UART_Init(&g_uart3_handle);
}

uint16_t modbus_crc16(const uint8_t *data, uint16_t len)
{
    uint16_t crc = 0xFFFF;
    uint16_t i;
    uint8_t  j;

    for (i = 0; i < len; i++)
    {
        crc ^= data[i];
        for (j = 0; j < 8; j++)
        {
            if (crc & 0x0001)
                crc = (crc >> 1) ^ 0xA001;
            else
                crc = crc >> 1;
        }
    }
    return crc;
}

int modbus_read_regs(uint8_t slave, uint16_t reg, uint16_t count, uint16_t *out)
{
    uint8_t  req[8];
    uint8_t  rsp[40];
    uint16_t crc;
    uint16_t need;
    uint16_t i;

    if (count == 0 || count > 16)
        return -1;

    /* ① 拼请求帧的前 6 个字节 */
    req[0] = slave;
    req[1] = 0x03;
    req[2] = (uint8_t)(reg >> 8);
    req[3] = (uint8_t)(reg & 0xFF);
    req[4] = (uint8_t)(count >> 8);
    req[5] = (uint8_t)(count & 0xFF);

    /* ② 算 CRC 并贴到末尾, 低字节在前 */
    crc = modbus_crc16(req, 6);
    req[6] = (uint8_t)(crc & 0xFF);
    req[7] = (uint8_t)(crc >> 8);

    /* ③ 从串口3 发出去 */
    if (HAL_UART_Transmit(&g_uart3_handle, req, 8, 100) != HAL_OK)
        return -2;

    /* ④ 等从站回话, 收满 need 个字节 */
    need = (uint16_t)(5 + 2 * count);
    if (HAL_UART_Receive(&g_uart3_handle, rsp, need, 200) != HAL_OK)
        return -3;

    /* ⑤ 检查回话对不对 */
    if (rsp[0] != slave)
        return -4;
    if (rsp[1] != 0x03 || rsp[2] != (uint8_t)(2 * count))
        return -5;

    crc = modbus_crc16(rsp, (uint16_t)(3 + 2 * count));
    if (rsp[3 + 2 * count] != (uint8_t)(crc & 0xFF) ||
        rsp[4 + 2 * count] != (uint8_t)(crc >> 8))
        return -6;

    /* ⑥ 把字节拼成 16 位数, 存进 out */
    for (i = 0; i < count; i++)
    {
        out[i] = ((uint16_t)rsp[3 + 2 * i] << 8) | rsp[4 + 2 * i];
    }

    return 0;
}
