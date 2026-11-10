#include "can_bsp.h"
#include "stm32f1xx_hal.h"

static CAN_HandleTypeDef hcan;

/* PA11 = CAN_RX, PA12 = CAN_TX。500 kbps: APB1 36MHz / 6 = 6MHz, 每 bit 12 个 TQ */
uint8_t can_bsp_init(void)
{
    CAN_FilterTypeDef f;
    GPIO_InitTypeDef  g;

    __HAL_RCC_CAN1_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();

    g.Pin  = GPIO_PIN_11;               /* CAN_RX: 输入上拉 */
    g.Mode = GPIO_MODE_INPUT;
    g.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(GPIOA, &g);

    g.Pin   = GPIO_PIN_12;              /* CAN_TX: 复用推挽 */
    g.Mode  = GPIO_MODE_AF_PP;
    g.Pull  = GPIO_NOPULL;
    g.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOA, &g);

    hcan.Instance = CAN1;
    hcan.Init.Prescaler         = 6;
    /* 单节点自测用回环: 自己发自己收。接真实总线时改成 CAN_MODE_NORMAL */
    hcan.Init.Mode              = CAN_MODE_LOOPBACK;
    hcan.Init.SyncJumpWidth     = CAN_SJW_1TQ;
    hcan.Init.TimeSeg1          = CAN_BS1_9TQ;
    hcan.Init.TimeSeg2          = CAN_BS2_2TQ;
    hcan.Init.TimeTriggeredMode = DISABLE;
    hcan.Init.AutoBusOff        = ENABLE;
    hcan.Init.AutoWakeUp        = DISABLE;
    hcan.Init.AutoRetransmission= ENABLE;
    hcan.Init.ReceiveFifoLocked = DISABLE;
    hcan.Init.TransmitFifoPriority = DISABLE;
    if (HAL_CAN_Init(&hcan) != HAL_OK)
    {
        return 1;
    }

    f.FilterBank           = 0;         /* 全部接收 */
    f.FilterMode           = CAN_FILTERMODE_IDMASK;
    f.FilterScale          = CAN_FILTERSCALE_32BIT;
    f.FilterIdHigh         = 0;
    f.FilterIdLow          = 0;
    f.FilterMaskIdHigh     = 0;
    f.FilterMaskIdLow      = 0;
    f.FilterFIFOAssignment = CAN_RX_FIFO0;
    f.FilterActivation     = CAN_FILTER_ENABLE;
    f.SlaveStartFilterBank = 14;
    if (HAL_CAN_ConfigFilter(&hcan, &f) != HAL_OK)
    {
        return 2;
    }
    if (HAL_CAN_Start(&hcan) != HAL_OK)
    {
        return 3;
    }

    return 0;
}

uint8_t can_bsp_send(uint32_t id, const uint8_t *data, uint8_t len)
{
    CAN_TxHeaderTypeDef tx;
    uint32_t mailbox;

    if (len > 8)
    {
        return 1;
    }

    tx.StdId              = id;
    tx.ExtId              = 0;
    tx.IDE                = CAN_ID_STD;
    tx.RTR                = CAN_RTR_DATA;
    tx.DLC                = len;
    tx.TransmitGlobalTime = DISABLE;
    if (HAL_CAN_AddTxMessage(&hcan, &tx, (uint8_t *)data, &mailbox) != HAL_OK)
    {
        return 2;
    }
    return 0;
}

uint8_t can_bsp_recv(uint32_t *id, uint8_t *data, uint8_t len, uint32_t timeout_ms)
{
    CAN_RxHeaderTypeDef rx;
    uint32_t t0 = HAL_GetTick();
    uint8_t  i;

    while (HAL_CAN_GetRxFifoFillLevel(&hcan, CAN_RX_FIFO0) == 0)
    {
        if ((HAL_GetTick() - t0) >= timeout_ms)
        {
            return 1;
        }
    }
    if (HAL_CAN_GetRxMessage(&hcan, CAN_RX_FIFO0, &rx, data) != HAL_OK)
    {
        return 2;
    }

    *id = (rx.IDE == CAN_ID_EXT) ? rx.ExtId : rx.StdId;
    for (i = (uint8_t)rx.DLC; i < len; i++)     /* 没用的字节清零, 打印好看 */
    {
        data[i] = 0;
    }
    return 0;
}
