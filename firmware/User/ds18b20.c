#include "ds18b20.h"
#include "stm32f1xx_hal.h"

/* DQ 接 PA1。CRL 里第 1 位对应 bits[7:4]: 0x3=推挽输出50MHz, 0x8=上拉输入 */
#define DQ_H()        (GPIOA->BSRR = GPIO_PIN_1)
#define DQ_L()        (GPIOA->BRR  = GPIO_PIN_1)
#define DQ_OUT()      (GPIOA->CRL = (GPIOA->CRL & ~(0xFu << 4)) | (0x3u << 4))
#define DQ_IN()       (GPIOA->CRL = (GPIOA->CRL & ~(0xFu << 4)) | (0x8u << 4))
#define DQ_READ()     ((GPIOA->IDR & GPIO_PIN_1) != 0)

#define CMD_SKIP_ROM  0xCC
#define CMD_CONVERT   0x44
#define CMD_READ_SCR  0xBE

/* 用内核自带的周期计数器计时, 不受 FreeRTOS 改动 SysTick 的影响 */
static void us_delay(uint32_t us)
{
    uint32_t start = DWT->CYCCNT;
    uint32_t ticks = us * (SystemCoreClock / 1000000u);
    while ((DWT->CYCCNT - start) < ticks)
    {
    }
}

static uint8_t ow_reset(void)
{
    uint8_t presence;

    DQ_OUT();
    DQ_L();
    us_delay(750);
    DQ_H();
    us_delay(70);                 /* 等器件拉低应答 */
    DQ_IN();
    presence = (DQ_READ() == 0);
    us_delay(410);

    return presence;
}

static void ow_write(uint8_t dat)
{
    uint8_t i;

    DQ_OUT();
    for (i = 0; i < 8; i++)
    {
        if (dat & 0x01)
        {
            DQ_L(); us_delay(2);
            DQ_H(); us_delay(60);
        }
        else
        {
            DQ_L(); us_delay(60);
            DQ_H(); us_delay(2);
        }
        dat >>= 1;
    }
}

static uint8_t ow_read(void)
{
    uint8_t i, dat = 0;

    for (i = 0; i < 8; i++)
    {
        DQ_OUT();
        DQ_L();
        us_delay(2);
        DQ_H();
        DQ_IN();
        us_delay(12);
        dat >>= 1;
        if (DQ_READ())
        {
            dat |= 0x80;
        }
        us_delay(50);
    }

    return dat;
}

/* Dallas CRC8, 多项式 x^8+x^5+x^4+1 */
static uint8_t crc8(const uint8_t *p, uint8_t n)
{
    uint8_t i, j, c = 0;

    for (i = 0; i < n; i++)
    {
        uint8_t b = p[i];
        for (j = 0; j < 8; j++)
        {
            uint8_t mix = (c ^ b) & 0x01;
            c >>= 1;
            if (mix)
            {
                c ^= 0x8C;
            }
            b >>= 1;
        }
    }

    return c;
}

uint8_t ds18b20_init(void)
{
    GPIO_InitTypeDef gpio = {0};

    __HAL_RCC_GPIOA_CLK_ENABLE();

    gpio.Pin   = GPIO_PIN_1;
    gpio.Mode  = GPIO_MODE_OUTPUT_PP;
    gpio.Pull  = GPIO_PULLUP;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOA, &gpio);
    DQ_H();

    /* 打开 DWT 周期计数器 */
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;

    return ow_reset();
}

void ds18b20_start(void)
{
    if (ow_reset())
    {
        ow_write(CMD_SKIP_ROM);
        ow_write(CMD_CONVERT);
    }
}

int16_t ds18b20_read_x10(void)
{
    uint8_t s[9];
    uint8_t i;
    int16_t raw;

    if (!ow_reset())
    {
        return DS18B20_ERR;
    }

    ow_write(CMD_SKIP_ROM);
    ow_write(CMD_READ_SCR);
    for (i = 0; i < 9; i++)
    {
        s[i] = ow_read();
    }

    if (crc8(s, 8) != s[8])
    {
        return DS18B20_ERR;
    }

    raw = (int16_t)(((uint16_t)s[1] << 8) | s[0]);
    return (int16_t)(((int32_t)raw * 10) / 16);   /* 12 位分辨率, 1 LSB = 0.0625C */
}
