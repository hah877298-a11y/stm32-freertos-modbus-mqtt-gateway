#ifndef __DS18B20_H
#define __DS18B20_H

#include <stdint.h>

#define DS18B20_ERR   ((int16_t)-32768)

uint8_t ds18b20_init(void);      /* 1 = 总线上找到了器件 */
void    ds18b20_start(void);     /* 发起一次转换, 等 750ms 再来读 */
int16_t ds18b20_read_x10(void);  /* 温度 x10 (256 = 25.6C), 失败返回 DS18B20_ERR */

#endif
