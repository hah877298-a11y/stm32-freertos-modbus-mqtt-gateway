#include "app_rtos.h"
#include "FreeRTOS.h"
#include "task.h"
#include "led.h"
#include "key.h"
#include "modbus_rtu.h"  
#include <stdio.h>
#include "i2c_bsp.h"
#include "gateway_state.h"
#include "ds18b20.h"
#include "esp_at.h"
#include "stm32f1xx_hal.h"
#include "lcd.h"
#include <string.h>

/* 栈单位是“字”(1 字 = 4 字节) */
#define LED_TASK_STACK       128
#define LED_TASK_PRIO        1
#define PRINT_TASK_STACK     256      /* 里面有 printf, 多给点 */
#define PRINT_TASK_PRIO      2
#define KEY_TASK_STACK       192
#define KEY_TASK_PRIO        3        /* 按键要最快 */

#define MB_TASK_STACK        192
#define MB_TASK_PRIO         2

#define I2C_TASK_STACK       256
#define I2C_TASK_PRIO        1

#define LCD_TASK_STACK       320
#define LCD_TASK_PRIO        1

static TaskHandle_t led_handle   = NULL;
static TaskHandle_t print_handle = NULL;
static TaskHandle_t key_handle   = NULL;
static TaskHandle_t mb_handle = NULL;
static volatile uint8_t g_led_cmd = 0;   /* 1 = 远程要求 LED0 常亮 */

static void task_led(void *pvParameters)
{
	gateway_state_t st;
    (void)pvParameters;               /* 这个任务不用参数, 消掉警告 */

    for (;;)                          /* 任务必须死循环, 绝对不能 return */
    {
		gateway_state_snapshot(&st);
        if (g_led_cmd) { LED0(0); }        /* 远程命令: 常亮 */
        else           { LED0_TOGGLE(); }
		LED1((st.alarm ? 0 : 1));
        vTaskDelay(500);              /* 500 个 tick = 500ms */
    }
}

static void task_print(void *pvParameters)
{
    uint32_t count = 0;
    gateway_state_t st;
    (void)pvParameters;

    for (;;)
    {
        vTaskDelay(1000);          /* 先睡 1 秒。是 vTaskDelay，不是 delay_ms */
        count++;
        printf("rtos tick=%lu\r\n", (unsigned long)count);

        if ((count % 5) == 0)      /* 每 5 秒做一次健康检查 */
        {
             gateway_state_snapshot(&st);
             printf("gw mb=%u t=%u.%u h=%u.%u vib=%u alarm=%u\r\n",
                    (unsigned int)st.modbus_ok,
                    (unsigned int)(gw_temp_x10(&st) / 10), (unsigned int)(gw_temp_x10(&st) % 10),
                    (unsigned int)(st.modbus_humidity_x10 / 10), (unsigned int)(st.modbus_humidity_x10 % 10),
                    (unsigned int)st.vibration_mg,
                    (unsigned int)st.alarm);

             printf("heap=%u stack=%u/%u/%u\r\n",
                   (unsigned int)xPortGetFreeHeapSize(),
                   (unsigned int)uxTaskGetStackHighWaterMark(led_handle),
                   (unsigned int)uxTaskGetStackHighWaterMark(print_handle),
                   (unsigned int)uxTaskGetStackHighWaterMark(key_handle));
        }
    }
}

static void task_key(void *pvParameters)
{
    uint32_t press_count = 0;
    (void)pvParameters;

    for (;;)
    {
        if (key_scan(0) == KEY1_PRES)
        {
            press_count++;
            printf("KEY1 #%lu pressed\r\n", (unsigned long)press_count);
            //LED1_TOGGLE();
        }

        vTaskDelay(20);
    }
}

static void task_modbus(void *pvParameters)
{
    uint16_t regs[3];
    int ret;
    int16_t t_local;

    (void)pvParameters;

    for (;;)
    {
        /* 本地 DS18B20: 发起转换 -> 等 750ms -> 读回 */
        ds18b20_start();
        vTaskDelay(800);
        t_local = ds18b20_read_x10();
        gateway_state_set_temp_local(t_local, (uint8_t)(t_local != DS18B20_ERR));

        ret = modbus_read_regs(1, 0, 3, regs);

        gateway_state_set_modbus(regs, (ret == 0) ? 1 : 0);

        if (ret == 0)
        {
            printf("mb ok t=%u.%u h=%u.%u st=%u\r\n",
                   (unsigned int)(regs[0] / 10), (unsigned int)(regs[0] % 10),
                   (unsigned int)(regs[1] / 10), (unsigned int)(regs[1] % 10),
                   (unsigned int)regs[2]);
        }
        else
        {
            printf("mb fail %d\r\n", ret);
        }

        vTaskDelay(2000);
    }
}

/* 整数开平方(牛顿迭代的位运算版)。不用 sqrtf 是为了不引入浮点库, 省 Flash 和栈 */
static uint32_t isqrt32(uint32_t x)
{
    uint32_t r = 0;
    uint32_t bit = 1UL << 30;

    while (bit > x)
    {
        bit >>= 2;
    }
    while (bit != 0)
    {
        if (x >= r + bit)
        {
            x -= r + bit;
            r  = (r >> 1) + bit;
        }
        else
        {
            r >>= 1;
        }
        bit >>= 2;
    }

    return r;
}

static void task_i2c(void *pvParameters)
{
    uint8_t list[8];
    uint8_t n;
    uint8_t i;
    uint8_t id;
    uint8_t buf[6];
    int16_t ax, ay, az;
    uint32_t sq, mag;
    uint32_t vib;
    int32_t mg;

    (void)pvParameters;

    for (;;)
    {
        n = i2c_scan(list, 8);

        printf("i2c scan: %u found ->", (unsigned int)n);
        for (i = 0; i < n && i < 8; i++)
        {
            printf(" 0x%02X", list[i]);
        }
        printf("\r\n");

        /* 读 WHO_AM_I(0x75) 认芯片: 0x68=MPU6050, 0x70=MPU6500, 0x71=MPU9250 */
        if (i2c_read_regs(0x68, 0x75, &id, 1) == 0)
        {
            printf("mpu id=0x%02X\r\n", id);
            i2c_write_reg(0x68, 0x6B, 0x00);   /* 唤醒芯片, 它上电默认在睡眠 */

            /* 0x3B 起连续 6 个字节 = X/Y/Z 三轴加速度, 每轴高字节在前 */
            if (i2c_read_regs(0x68, 0x3B, buf, 6) == 0)
            {
                ax = (int16_t)((buf[0] << 8) | buf[1]);
                ay = (int16_t)((buf[2] << 8) | buf[3]);
                az = (int16_t)((buf[4] << 8) | buf[5]);
                printf("acc x=%d y=%d z=%d\r\n", ax, ay, az);

                /* 振动强度 = 三轴合成向量的模长 - 1g。静止时约等于 0, 晃动时变大 */
                sq  = (uint32_t)((int32_t)ax * ax) + (uint32_t)((int32_t)ay * ay) + (uint32_t)((int32_t)az * az);
                mag = isqrt32(sq);          /* 单位是 LSB, 1g = 16384 */
                mg  = (int32_t)mag - 16384;
                if (mg < 0)
                {
                    mg = -mg;               /* 倾斜时可能略小于 1g, 取绝对值免得出现"负振动" */
                }
                vib = (uint32_t)mg * 1000 / 16384;
                printf("vib=%u mg\r\n", (unsigned int)vib);
                gateway_state_set_vib((uint16_t)vib, 1);
            }
            else
            {
                printf("acc read fail\r\n");
            }
        }
        else
        {
            printf("mpu id read fail\r\n");
        }

        vTaskDelay(5000);
    }
}
/* ESP 驱动在 esp_at.c, 这里只管周期性发 AT 并打印结果 */
static const char *k_esp_cmds[] = {
    "AT\r\n",
    "ATE0\r\n",
    "AT+CWMODE=1\r\n",
    "AT+CWJAP?\r\n",
    "AT+CIPMODE=1\r\n",
    "AT+CIPSTART=\"TCP\",\"broker.emqx.io\",1883\r\n",
    "AT+CIPSEND\r\n",
};

static uint16_t g_pid;                  /* QoS1 报文号 */
static volatile uint8_t g_pub_ok;       /* 1 = 服务器回了 PUBACK, 数据真的送到了 */

/* QoS1 发布: 0x32 + 总长度 + 主题长度(2字节) + 主题 + 报文号(2字节) + 内容 */
static void mqtt_publish(const char *topic, const char *payload)
{
    uint8_t  pkt[160];
    uint16_t tlen = (uint16_t)strlen(topic);
    uint16_t plen = (uint16_t)strlen(payload);
    uint16_t i = 0;

    g_pid++;
    if (g_pid == 0)
    {
        g_pid = 1;
    }
    g_pub_ok = 0;

    pkt[i++] = 0x32;
    pkt[i++] = (uint8_t)(2 + 2 + tlen + plen);
    pkt[i++] = (uint8_t)(tlen >> 8);
    pkt[i++] = (uint8_t)(tlen & 0xFF);
    memcpy(&pkt[i], topic, tlen); i += tlen;
    pkt[i++] = (uint8_t)(g_pid >> 8);
    pkt[i++] = (uint8_t)(g_pid & 0xFF);
    memcpy(&pkt[i], payload, plen); i += plen;
    esp_at_send(pkt, i);
}
/* MQTT CONNECT: 协议名 MQTT / 版本 4 / 心跳 60s / 客户端名 gw01 */
static const uint8_t k_mqtt_connect[] = {
    0x10, 0x10,
    0x00, 0x04, 'M', 'Q', 'T', 'T',
    0x04, 0x02, 0x00, 0x3C,
    0x00, 0x04, 'g', 'w', '0', '1'
};
/* MQTT SUBSCRIBE: 订阅 stm32/gw01/cmd, QoS0 */
static const uint8_t k_mqtt_sub[] = {
    0x82, 0x13, 0x00, 0x01,
    0x00, 0x0E, 's','t','m','3','2','/','g','w','0','1','/','c','m','d',
    0x00
};

static volatile uint8_t g_net_up;     /* 1 = Wi-Fi+TCP+MQTT 已连上 */
static volatile uint8_t g_net_lost;   /* 1 = ESP 报告连接已断 */

/* 在二进制报文里找子串: 报文含 0x00, 不能用 strstr */
static int buf_has(const char *b, int n, const char *pat)
{
    int i, m = (int)strlen(pat);

    for (i = 0; i + m <= n; i++)
    {
        if (memcmp(&b[i], pat, m) == 0)
        {
            return 1;
        }
    }
    return 0;
}

/* 远程命令: 只认 {"led":1} / {"led":0}, 收到就改 g_led_cmd */
static void mqtt_cmd_parse(const char *b, int n)
{
    int i;

    for (i = 0; i + 6 < n; i++)
    {
        if (memcmp(&b[i], "\"led\":", 6) == 0)
        {
            g_led_cmd = (b[i + 6] == '1') ? 1 : 0;
            printf("cmd led=%u\r\n", (unsigned)g_led_cmd);
            return;
        }
    }
}

static void mqtt_poll_cmd(void)
{
    char rx[96];
    int  n, i;

    n = esp_at_read(rx, sizeof(rx), 100);
    if (n <= 0)
    {
        return;
    }
    for (i = 0; i < n; i++)             /* PUBACK 首字节 0x40; ESP 可能把它拆成两包 */
    {
        if ((uint8_t)rx[i] == 0x40 && (i + 1 >= n || (uint8_t)rx[i + 1] == 0x02))
        {
            g_pub_ok = 1;               /* 同一时刻只有一条消息在飞, 不必核对报文号 */
            break;
        }
    }
    if (buf_has(rx, n, "CLOSED") || buf_has(rx, n, "WIFI DISCONNECT"))
    {
        g_net_lost = 1;                 /* 连接断了, 让上层重连 */
    }
    mqtt_cmd_parse(rx, n);
}

/* 连 Wi-Fi + TCP + MQTT. 成功返回 1, 失败返回 0 由调用者重试 */
static int esp_net_start(void)
{
    char buf[128];
    int  n, i, k;

    /* ESP 上次可能停在透传模式: 先 +++ 退出, 再关掉旧 TCP 连接 */
    esp_at_send("+++", 3);
    vTaskDelay(1200);
    esp_at_read(buf, sizeof(buf), 300);
    esp_at_cmd("AT+CIPCLOSE\r\n", buf, sizeof(buf), 1000);

    for (k = 0; k < (int)(sizeof(k_esp_cmds) / sizeof(k_esp_cmds[0])); k++)
    {
        n = esp_at_cmd(k_esp_cmds[k], buf, sizeof(buf), 8000);
        for (i = 0; i < n; i++)            /* 回车换行换成空格, 一行看完 */
        {
            if (buf[i] == '\r' || buf[i] == '\n')
            {
                buf[i] = ' ';
            }
        }
        printf("esp[%d] %d bytes: %s\r\n", k, n, buf);
        if (n <= 0)
        {
            return 0;                      /* 模块没回话, 重来 */
        }
        vTaskDelay(500);
    }

    /* 向 MQTT 服务器报到, 它应该回 20 02 00 00 */
    esp_at_flush();                                        /* 丢掉 CIPSEND 留下的 '>' */
    esp_at_send(k_mqtt_connect, sizeof(k_mqtt_connect));
    esp_at_send(k_mqtt_sub, sizeof(k_mqtt_sub));            /* 订阅命令主题 */
    n = esp_at_read(buf, sizeof(buf), 1000);
    printf("mqtt connack %d bytes:", n);
    for (i = 0; i < n; i++)
    {
        printf(" %02X", (uint8_t)buf[i]);
    }
    printf("\r\n");
    i = 0;                                 /* 前面可能混着 AT 残留字节, 往后找 CONNACK */
    while (i + 3 < n && !((uint8_t)buf[i] == 0x20 && (uint8_t)buf[i + 1] == 0x02)) i++;
    if (i + 3 >= n || (uint8_t)buf[i + 3] != 0x00)
    {
        return 0;                          /* 没找到 CONNACK, 或者服务器拒绝 */
    }
    mqtt_cmd_parse(buf, n);                /* 这包可能夹着第一条命令 */
    return 1;
}

static void task_esp(void *pvParameters)
{
    char js[128];
    uint32_t t0;
    uint8_t pub_fail = 0;
    (void)pvParameters;
    vTaskDelay(1000);

    for (;;)
    {
        gateway_state_t st;

        if (!g_net_up)
        {
            printf("esp connecting...\r\n");
            if (!esp_net_start())
            {
                printf("esp connect fail, retry\r\n");
                vTaskDelay(3000);
                continue;
            }
            g_net_up = 1;
            printf("esp online\r\n");
        }

        gateway_state_snapshot(&st);
        snprintf(js, sizeof(js),
                 "{\"t\":%.1f,\"h\":%.1f,\"vib\":%u,\"alarm\":%u,\"mb\":%u,\"up\":%lu}",
                 gw_temp_x10(&st) / 10.0f,
                 st.modbus_humidity_x10 / 10.0f,
                 (unsigned)st.vibration_mg,
                 (unsigned)st.alarm,
                 (unsigned)st.modbus_ok,
                 (unsigned long)(xTaskGetTickCount() / configTICK_RATE_HZ));

        mqtt_publish("stm32/gw01/telemetry", js);
        printf("pub: %s\r\n", js);

        t0 = xTaskGetTickCount();               /* 固定 5 秒一个周期: 发布 + 收命令 */
        while ((xTaskGetTickCount() - t0) < 5000 && !g_net_lost)
        {
            mqtt_poll_cmd();
        }
        if (g_net_lost)                     /* ESP 明确说 TCP 断了 */
        {
            printf("net lost (tcp closed), reconnect\r\n");
            g_net_lost = 0;
            g_net_up   = 0;
            pub_fail   = 0;
        }
        else if (g_pub_ok)                 /* 服务器确认收到 */
        {
            pub_fail = 0;
        }
        else if (++pub_fail >= 3)          /* 连发 3 条都没确认: 连接已是死的 */
        {
            printf("no puback x3, reconnect\r\n");
            pub_fail = 0;
            g_net_up = 0;
        }
    }
}

/* 屏幕: 每 500ms 把最新状态刷到 LCD 上 (字库只有 ASCII, 所以用英文) */
static void task_lcd(void *pvParameters)
{
    gateway_state_t st;
    char s[24];
    (void)pvParameters;

    g_back_color = WHITE;                /* 字后面自动补白底, 不会留残影 */
    lcd_clear(WHITE);
    lcd_show_string(8, 8, 224, 24, 24, "IOT GATEWAY", BLUE);
    lcd_draw_hline(8, 40, 224, GRAY);

    for (;;)
    {
        gateway_state_snapshot(&st);

        snprintf(s, sizeof(s), "TEMP  : %2u.%u C",
                 (unsigned)(gw_temp_x10(&st) / 10), (unsigned)(gw_temp_x10(&st) % 10));
        lcd_show_string(8, 50, 224, 16, 16, s, BLACK);

        snprintf(s, sizeof(s), "HUMI  : %2u.%u %%",
                 (unsigned)(st.modbus_humidity_x10 / 10), (unsigned)(st.modbus_humidity_x10 % 10));
        lcd_show_string(8, 72, 224, 16, 16, s, BLACK);

        snprintf(s, sizeof(s), "VIB   : %3u mg", (unsigned)st.vibration_mg);
        lcd_show_string(8, 94, 224, 16, 16, s, BLACK);

        snprintf(s, sizeof(s), "MODBUS: %s", st.modbus_ok ? "OK " : "ERR");
        lcd_show_string(8, 116, 224, 16, 16, s, st.modbus_ok ? BLACK : RED);

        snprintf(s, sizeof(s), "NET   : %s", g_net_up ? "ONLINE " : "OFFLINE");
        lcd_show_string(8, 138, 224, 16, 16, s, g_net_up ? BLACK : RED);

        snprintf(s, sizeof(s), "ALARM : %s", st.alarm ? "ON " : "OFF");
        lcd_show_string(8, 160, 224, 16, 16, s, st.alarm ? RED : BLACK);

        snprintf(s, sizeof(s), "UP    : %lu s",
                 (unsigned long)(xTaskGetTickCount() / configTICK_RATE_HZ));
        lcd_show_string(8, 182, 224, 16, 16, s, BLACK);

        vTaskDelay(500);
    }
}
/* 栈溢出时 FreeRTOS 自动调用这里: 点亮 LED1 并停住 */
void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName)
{
    (void)xTask;
    (void)pcTaskName;
    LED1(0);            /* 低电平点亮, 所以是 0 */
    for (;;)
    {
    }
}

/* 堆不够时 FreeRTOS 自动调用这里 */
void vApplicationMallocFailedHook(void)
{
    LED1(0);
    for (;;)
    {
    }
}

void app_rtos_start(void)
{
	static const uint8_t test_frame[6] = {0x01, 0x03, 0x00, 0x00, 0x00, 0x03};

    i2c_bsp_init();
    gateway_state_init();
    printf("ds18b20 %s\r\n", ds18b20_init() ? "found" : "not found");
    modbus_rtu_init();
    esp_at_init();
    printf("crc selfcheck=%04X (expect CB05)\r\n", modbus_crc16(test_frame, 6));
	
    xTaskCreate((TaskFunction_t)task_led,   "led",   LED_TASK_STACK,   NULL, LED_TASK_PRIO,   &led_handle);
    xTaskCreate((TaskFunction_t)task_print, "print", PRINT_TASK_STACK, NULL, PRINT_TASK_PRIO, &print_handle);
    xTaskCreate((TaskFunction_t)task_key,   "key",   KEY_TASK_STACK,   NULL, KEY_TASK_PRIO,   &key_handle);
    xTaskCreate((TaskFunction_t)task_modbus, "mb", MB_TASK_STACK, NULL, MB_TASK_PRIO, &mb_handle);
    xTaskCreate((TaskFunction_t)task_i2c,  "i2c", I2C_TASK_STACK, NULL, I2C_TASK_PRIO, NULL);
    xTaskCreate((TaskFunction_t)task_esp,  "esp", 512, NULL, 1, NULL);
    xTaskCreate((TaskFunction_t)task_lcd,  "lcd", LCD_TASK_STACK, NULL, LCD_TASK_PRIO, NULL);   /* 有 printf 浮点, 256 会溢出 */
    
	vTaskStartScheduler();      /* 到这里交给调度器, 正常不返回 */
}
