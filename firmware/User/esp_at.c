#include "esp_at.h"
#include "stm32f1xx_hal.h"
#include <string.h>

/* ESP-01S 接在 UART4: PC10=TX, PC11=RX, 115200 8N1 */
static UART_HandleTypeDef g_uart4;

/* 中断收 + 环形缓冲: 别的任务在打印时, 收到的字节也不会丢 */
#define RX_RING 256
static uint8_t  g_rxbuf[RX_RING];
static volatile uint16_t g_rxhead, g_rxtail;
static uint8_t  g_rxbyte;

static void rx_start(void)
{
    HAL_UART_Receive_IT(&g_uart4, &g_rxbyte, 1);
}

/* 收到 1 个字节进中断一次: 存起来, 再挂下一次 */
void esp_at_rx_byte(void)
{
    uint16_t next;

    next = (uint16_t)((g_rxhead + 1) % RX_RING);
    if (next != g_rxtail)              /* 缓冲区满了就丢新字节, 不覆盖旧数据 */
    {
        g_rxbuf[g_rxhead] = g_rxbyte;
        g_rxhead = next;
    }
    rx_start();
}

/* 溢出/噪声等错误后, 重新把接收挂起来, 否则会永远收不到 */
void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == UART4)
    {
        __HAL_UART_CLEAR_OREFLAG(huart);
        rx_start();
    }
}

void esp_at_irq_handler(void)
{
    HAL_UART_IRQHandler(&g_uart4);
}

void esp_at_init(void)
{
    GPIO_InitTypeDef gpio = {0};

    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_UART4_CLK_ENABLE();

    gpio.Pin   = GPIO_PIN_10;              /* UART4_TX */
    gpio.Mode  = GPIO_MODE_AF_PP;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOC, &gpio);

    gpio.Pin  = GPIO_PIN_11;               /* UART4_RX */
    gpio.Mode = GPIO_MODE_INPUT;
    gpio.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(GPIOC, &gpio);

    g_uart4.Instance          = UART4;
    g_uart4.Init.BaudRate     = 115200;
    g_uart4.Init.WordLength   = UART_WORDLENGTH_8B;
    g_uart4.Init.StopBits     = UART_STOPBITS_1;
    g_uart4.Init.Parity       = UART_PARITY_NONE;
    g_uart4.Init.HwFlowCtl    = UART_HWCONTROL_NONE;
    g_uart4.Init.Mode         = UART_MODE_TX_RX;
    HAL_UART_Init(&g_uart4);

    HAL_NVIC_SetPriority(UART4_IRQn, 6, 0);   /* 不在中断里调用 RTOS 接口 */
    HAL_NVIC_EnableIRQ(UART4_IRQn);
    rx_start();
}

void esp_at_send(const void *data, uint16_t len)
{
    HAL_UART_Transmit(&g_uart4, (uint8_t *)data, len, 200);
}

/* 只收不发: 等最多 timeout_ms 毫秒, 从环形缓冲取字节放进 resp, 返回个数 */
int esp_at_read(char *resp, uint16_t size, uint32_t timeout_ms)
{
    uint16_t n = 0;
    uint32_t start = HAL_GetTick();

    resp[0] = 0;
    while (n < (uint16_t)(size - 1) && (HAL_GetTick() - start) < timeout_ms)
    {
        if (g_rxtail != g_rxhead)
        {
            char ch = (char)g_rxbuf[g_rxtail];
            g_rxtail = (uint16_t)((g_rxtail + 1) % RX_RING);
            resp[n++] = ch;
            if (ch == '>' || (ch == '\n' && n >= 4 && resp[n - 4] == 'O' && resp[n - 3] == 'K'))
            {
                break;              /* 收到 OK 或 > 就不用再等 */
            }
        }
    }
    resp[n] = 0;
    return n;
}

/* 丢掉缓冲区里的残留字节 (比如 CIPSEND 后面那个 '>' 提示符) */
void esp_at_flush(void)
{
    g_rxtail = g_rxhead;
}

/* 发一条命令, 等它的回应 */
int esp_at_cmd(const char *cmd, char *resp, uint16_t size, uint32_t timeout_ms)
{
    esp_at_send(cmd, (uint16_t)strlen(cmd));
    return esp_at_read(resp, size, timeout_ms);
}
