#include "i2c_bsp.h"
#include "stm32f1xx_hal.h"
#include "delay.h"

/* 软件模拟 I2C: 用两个普通 IO 手动拉高拉低, 不依赖 I2C 外设。
   正点原子官方 IIC 例程也是这样做的, 在 F103 上比 HAL 硬件 I2C 稳。 */
#define I2C_PORT        GPIOB
#define I2C_SCL_PIN     GPIO_PIN_6
#define I2C_SDA_PIN     GPIO_PIN_7

#define SCL_H()   HAL_GPIO_WritePin(I2C_PORT, I2C_SCL_PIN, GPIO_PIN_SET)
#define SCL_L()   HAL_GPIO_WritePin(I2C_PORT, I2C_SCL_PIN, GPIO_PIN_RESET)
#define SDA_H()   HAL_GPIO_WritePin(I2C_PORT, I2C_SDA_PIN, GPIO_PIN_SET)
#define SDA_L()   HAL_GPIO_WritePin(I2C_PORT, I2C_SDA_PIN, GPIO_PIN_RESET)
#define SDA_READ() HAL_GPIO_ReadPin(I2C_PORT, I2C_SDA_PIN)

/* 半个时钟周期 2us, 大约 125kHz, 比 400kHz 慢但绝对稳 */
static void i2c_delay(void)
{
    delay_us(2);
}

/* 开漏输出: 写 1 = 松手让上拉电阻拉高, 写 0 = 主动拉低。
   这样同一根线既能输出又能读回真实电平, 这是软件 I2C 的关键。 */
void i2c_bsp_init(void)
{
    GPIO_InitTypeDef gpio;

    __HAL_RCC_GPIOB_CLK_ENABLE();

    gpio.Pin   = I2C_SCL_PIN | I2C_SDA_PIN;
    gpio.Mode  = GPIO_MODE_OUTPUT_OD;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(I2C_PORT, &gpio);

    SCL_H();
    SDA_H();
}

/* 起始条件: SCL 高电平期间, SDA 由高变低 */
static void i2c_start(void)
{
    SDA_H();
    SCL_H();
    i2c_delay();
    SDA_L();
    i2c_delay();
    SCL_L();
    i2c_delay();
}

/* 停止条件: SCL 高电平期间, SDA 由低变高 */
static void i2c_stop(void)
{
    SCL_L();
    SDA_L();
    i2c_delay();
    SCL_H();
    i2c_delay();
    SDA_H();
    i2c_delay();
}

/* 发一个字节, 返回 1 = 从机回了 ACK(有人), 0 = 没人应答 */
static uint8_t i2c_send_byte(uint8_t byte)
{
    uint8_t i;
    uint8_t ack;

    for (i = 0; i < 8; i++)                 /* 高位先发 */
    {
        if (byte & 0x80) { SDA_H(); }
        else             { SDA_L(); }
        byte <<= 1;
        i2c_delay();
        SCL_H();
        i2c_delay();
        SCL_L();
        i2c_delay();
    }

    SDA_H();                                /* 第 9 个时钟: 松手, 让从机拉低表示 ACK */
    i2c_delay();
    SCL_H();
    i2c_delay();
    ack = (SDA_READ() == GPIO_PIN_RESET) ? 1 : 0;
    SCL_L();
    i2c_delay();

    return ack;
}

/* 从 0x08 扫到 0x77, 有应答的地址存进 out, 返回找到几个 */
uint8_t i2c_scan(uint8_t *out, uint8_t max)
{
    uint8_t addr;
    uint8_t n = 0;

    for (addr = 0x08; addr <= 0x77; addr++)
    {
        i2c_start();
        if (i2c_send_byte((uint8_t)(addr << 1)))
        {
            if (n < max)
            {
                out[n] = addr;
            }
            n++;
        }
        i2c_stop();
    }

    return n;
}

/* 读一个字节。ack=1 表示"我还要继续读", 从机会接着给下一个字节 */
static uint8_t i2c_read_byte(uint8_t ack)
{
    uint8_t i;
    uint8_t byte = 0;

    SDA_H();                                /* 松手, 数据线交给从机驱动 */

    for (i = 0; i < 8; i++)
    {
        byte <<= 1;
        i2c_delay();
        SCL_H();
        i2c_delay();
        if (SDA_READ() == GPIO_PIN_SET)
        {
            byte |= 1;
        }
        SCL_L();
        i2c_delay();
    }

    if (ack) { SDA_L(); } else { SDA_H(); }  /* 第 9 个时钟: 主机应答 */
    i2c_delay();
    SCL_H();
    i2c_delay();
    SCL_L();
    i2c_delay();
    SDA_H();

    return byte;
}

/* 往从机的某个寄存器写一个字节 */
uint8_t i2c_write_reg(uint8_t dev, uint8_t reg, uint8_t val)
{
    i2c_start();
    if (!i2c_send_byte((uint8_t)(dev << 1))) { i2c_stop(); return 1; }
    if (!i2c_send_byte(reg))                 { i2c_stop(); return 1; }
    if (!i2c_send_byte(val))                 { i2c_stop(); return 1; }
    i2c_stop();

    return 0;
}

/* 从从机的某个寄存器开始, 连续读 len 个字节 */
uint8_t i2c_read_regs(uint8_t dev, uint8_t reg, uint8_t *buf, uint8_t len)
{
    uint8_t i;

    i2c_start();
    if (!i2c_send_byte((uint8_t)(dev << 1)))         { i2c_stop(); return 1; }  /* 先"写": 告诉它要读哪个寄存器 */
    if (!i2c_send_byte(reg))                          { i2c_stop(); return 1; }

    i2c_start();                                                                /* 重复起始 */
    if (!i2c_send_byte((uint8_t)((dev << 1) | 1)))   { i2c_stop(); return 1; }  /* 再"读": 最低位给 1 */

    for (i = 0; i < len; i++)
    {
        buf[i] = i2c_read_byte((i + 1 < len) ? 1 : 0);
    }
    i2c_stop();

    return 0;
}
