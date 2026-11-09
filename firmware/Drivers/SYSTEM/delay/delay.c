/**
 ****************************************************************************************************
 * @file        delay.c
 * @author      ÕıµãÔ­×ÓÍÅ¶Ó(ALIENTEK)
 * @version     V1.1
 * @date        2023-02-25
 * @brief       Ê¹ÓÃSysTickµÄÆÕÍ¨¼ÆÊıÄ£Ê½¶ÔÑÓ³Ù½øĞĞ¹ÜÀí(Ö§³Öucosii)
 *              Ìá¹©delay_init³õÊ¼»¯º¯Êı£¬ delay_usºÍdelay_msµÈÑÓÊ±º¯Êı
 * @license     Copyright (c) 2022-2032, ¹ãÖİÊĞĞÇÒíµç×Ó¿Æ¼¼ÓĞÏŞ¹«Ë¾
 ****************************************************************************************************
 * @attention
 *
 * ÊµÑéÆ½Ì¨:ÕıµãÔ­×Ó STM32F103¿ª·¢°å
 * ÔÚÏßÊÓÆµ:www.yuanzige.com
 * ¼¼ÊõÂÛÌ³:www.openedv.com
 * ¹«Ë¾ÍøÖ·:www.alientek.com
 * ¹ºÂòµØÖ·:openedv.taobao.com
 *
 * ĞŞ¸ÄËµÃ÷
 * V1.0 20230206
 * µÚÒ»´Î·¢²¼
 * V1.1 20230225
 * ĞŞ¸ÄSYS_SUPPORT_OS²¿·Ö´úÂë, Ä¬ÈÏ½öÖ§³ÖUCOSII 2.93.01°æ±¾, ÆäËûOSÇë²Î¿¼ÊµÏÖ
 * ĞŞ¸Ädelay_init²»ÔÙÊ¹ÓÃ8·ÖÆµ,È«²¿Í³Ò»Ê¹ÓÃMCUÊ±ÖÓ
 * ĞŞ¸Ädelay_usÊ¹ÓÃÊ±ÖÓÕªÈ¡·¨ÑÓÊ±, ¼æÈİOS
 * ĞŞ¸Ädelay_msÖ±½ÓÊ¹ÓÃdelay_usÑÓÊ±ÊµÏÖ.
 *
 ****************************************************************************************************
 */

#include "./SYSTEM/sys/sys.h"
#include "./SYSTEM/delay/delay.h"


static uint32_t g_fac_us = 0;       /* usÑÓÊ±±¶³ËÊı */

/* Èç¹ûSYS_SUPPORT_OS¶¨ÒåÁË,ËµÃ÷ÒªÖ§³ÖOSÁË(²»ÏŞÓÚUCOS) */
#if SYS_SUPPORT_OS

#include "FreeRTOS.h"
#include "task.h"

extern void xPortSysTickHandler(void);

/**
 * @brief     systickÖĞ¶Ï·şÎñº¯Êı,Ê¹ÓÃOSÊ±ÓÃµ½
 * @param     ticks : ÑÓÊ±µÄ½ÚÅÄÊı  
 * @retval    ÎŞ
 */  
void SysTick_Handler(void)
{
    HAL_IncTick();
	
    /* os å¼€å§‹è·‘äº†,æ‰æ‰§è¡Œæ­£å¸¸çš„è°ƒåº¦å¤„ç† */
	
    if (xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED)
   {
    xPortSysTickHandler();
   }

}
#endif

/**
 * @brief     ³õÊ¼»¯ÑÓ³Ùº¯Êı
 * @param     sysclk: ÏµÍ³Ê±ÖÓÆµÂÊ, ¼´CPUÆµÂÊ(rcc_c_ck), 72MHz
 * @retval    ÎŞ
 */  
void delay_init(uint16_t sysclk)
{
#if SYS_SUPPORT_OS
    uint32_t reload;
#endif
    SysTick->CTRL = 0;
    HAL_SYSTICK_CLKSourceConfig(SYSTICK_CLKSOURCE_HCLK_DIV8);

    g_fac_us = sysclk / 8;
#if SYS_SUPPORT_OS
    reload = sysclk / 8;
    /* ä½¿ç”¨ configTICK_RATE_HZ è®¡ç®—é‡è£…è½½å€¼
     * configTICK_RATE_HZ åœ¨ FreeRTOSConfig.h ä¸­å®šä¹‰
     */
    reload *= 1000000 / configTICK_RATE_HZ;
    /* åˆ é™¤ä¸ç”¨çš„ g_fac_ms ç›¸å…³ä»£ç  */
    SysTick->CTRL |= 1 << 1;
    SysTick->LOAD = reload;
    SysTick->CTRL |= 1 << 0;

#endif
}


/**
 * @brief     ÑÓÊ±nus
 * @note      ÎŞÂÛÊÇ·ñÊ¹ÓÃOS, ¶¼ÊÇÓÃÊ±ÖÓÕªÈ¡·¨À´×öusÑÓÊ±
 * @param     nus: ÒªÑÓÊ±µÄusÊı
 * @note      nusÈ¡Öµ·¶Î§: 0 ~ (2^32 / fac_us) (fac_usÒ»°ãµÈÓÚÏµÍ³Ö÷Æµ, ×ÔĞĞÌ×Èë¼ÆËã)
 * @retval    ÎŞ
 */
void delay_us(uint32_t nus)
{
    uint32_t ticks;
    uint32_t told, tnow, tcnt = 0;
    uint32_t reload = SysTick->LOAD;        /* LOADµÄÖµ */
    ticks = nus * g_fac_us;                 /* ĞèÒªµÄ½ÚÅÄÊı */
    told = SysTick->VAL;                    /* ¸Õ½øÈëÊ±µÄ¼ÆÊıÆ÷Öµ */
    while (1)
    {
        tnow = SysTick->VAL;
        if (tnow != told)
        {
            if (tnow < told)
            {
                tcnt += told - tnow;        /* ÕâÀï×¢ÒâÒ»ÏÂSYSTICKÊÇÒ»¸öµİ¼õµÄ¼ÆÊıÆ÷¾Í¿ÉÒÔÁË */
            }
            else
            {
                tcnt += reload - tnow + told;
            }
            told = tnow;
            if (tcnt >= ticks) 
            {
                break;                      /* Ê±¼ä³¬¹ı/µÈÓÚÒªÑÓ³ÙµÄÊ±¼ä,ÔòÍË³ö */
            }
        }
    }
}

/**
 * @brief     ÑÓÊ±nms
 * @param     nms: ÒªÑÓÊ±µÄmsÊı (0< nms <= (2^32 / fac_us / 1000))(fac_usÒ»°ãµÈÓÚÏµÍ³Ö÷Æµ, ×ÔĞĞÌ×Èë¼ÆËã)
 * @retval    ÎŞ
 */
void delay_ms(uint16_t nms)
{
     uint32_t i;

     for (i=0; i<nms; i++)
    {
       delay_us(1000);
    }
}

/**
 * @brief       HAL¿âÄÚ²¿º¯ÊıÓÃµ½µÄÑÓÊ±
 * @note        HAL¿âµÄÑÓÊ±Ä¬ÈÏÓÃSystick£¬Èç¹ûÎÒÃÇÃ»ÓĞ¿ªSystickµÄÖĞ¶Ï»áµ¼ÖÂµ÷ÓÃÕâ¸öÑÓÊ±ºóÎŞ·¨ÍË³ö
 * @param       Delay : ÒªÑÓÊ±µÄºÁÃëÊı
 * @retval      None
 */
void HAL_Delay(uint32_t Delay)
{
     delay_ms(Delay);
}










