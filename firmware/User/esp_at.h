#ifndef ESP_AT_H
#define ESP_AT_H

#include <stdint.h>

void esp_at_init(void);
void esp_at_irq_handler(void);
int  esp_at_cmd(const char *cmd, char *resp, uint16_t size, uint32_t timeout_ms);
void esp_at_send(const void *data, uint16_t len);
void esp_at_flush(void);
int  esp_at_read(char *resp, uint16_t size, uint32_t timeout_ms);

#endif
