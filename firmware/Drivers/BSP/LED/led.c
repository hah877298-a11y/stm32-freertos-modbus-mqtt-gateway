/**
 ****************************************************************************************************
 * @file        led.c
 * @author      ����ԭ���Ŷ�(ALIENTEK)
 * @version     V1.0
 * @date        2020-04-17
 * @brief       LED ��������
 * @license     Copyright (c) 2020-2032, �������������ӿƼ����޹�˾
 ****************************************************************************************************
 * @attention
 *
 * ʵ��ƽ̨:����ԭ�� STM32F103������
 * ������Ƶ:www.yuanzige.com
 * ������̳:www.openedv.com
 * ��˾��ַ:www.alientek.com
 * �����ַ:openedv.taobao.com
 *
 * �޸�˵��
 * V1.0 20200417
 * ��һ�η���
 *
 ****************************************************************************************************
 */

#include "./BSP/LED/led.h"


/**
 * @brief       ��ʼ��LED���IO��, ��ʹ��ʱ��
 * @param       ��
 * @retval      ��
 */
void led_init(void)
{
    GPIO_InitTypeDef gpio_init_struct = {0};

    LED0_GPIO_CLK_ENABLE();                             /* LED0时钟使能 */
    LED1_GPIO_CLK_ENABLE();                             /* LED1时钟使能 */

    gpio_init_struct.Pin = LED0_GPIO_PIN;               /* LED0引脚 */
    gpio_init_struct.Mode = GPIO_MODE_OUTPUT_PP;        /* 推挽输出 */
    gpio_init_struct.Pull = GPIO_PULLUP;                /* 上拉 */
    gpio_init_struct.Speed = GPIO_SPEED_FREQ_HIGH;      /* 高速 */
    HAL_GPIO_Init(LED0_GPIO_PORT, &gpio_init_struct);   /* 初始化LED0引脚 */

    gpio_init_struct.Pin = LED1_GPIO_PIN;               /* LED1引脚 */
    HAL_GPIO_Init(LED1_GPIO_PORT, &gpio_init_struct);   /* 初始化LED1引脚 */

    LED0(1);                                            /* 默认关闭 LED0 */
    LED1(1);                                            /* 默认关闭 LED1 */
}
