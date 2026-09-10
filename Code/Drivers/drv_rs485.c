#include "drv_rs485.h"

#include "main.h"
#include "usart.h"

/**
 * @brief 初始化RS485收发器电源、方向和串口接收。
 * @param 无。
 * @return 无返回值。
 */
void RS485_Initialize(void)
{
    RS485_PwrState(RS485_ENABLE_PWR);
    RS485_TransmitState(RS485_RX);
    USART2_StartReceive();
}

/**
 * @brief 控制RS485收发器电源。
 * @param NewState 目标使能状态。
 * @return 无返回值。
 */
void RS485_PwrState(RS485_PwrStateTypedef NewState)
{
    gpio_bit_write(RS485_PWREN_GPIO_Port, RS485_PWREN_Pin,
                   (NewState == RS485_ENABLE_PWR) ? RESET : SET);
}

/**
 * @brief 切换RS485收发方向。
 * @param NewState 目标使能状态。
 * @return 无返回值。
 */
void RS485_TransmitState(RS485_TransmitStateTypedef NewState)
{
    gpio_bit_write(RS485_EN_GPIO_Port, RS485_EN_Pin,
                   (NewState == RS485_RX) ? RESET : SET);
}

/**
 * @brief 保留的RS485发送测试入口。
 * @param 无。
 * @return 无返回值。
 */
void RS485_TransmitTest(void)
{
}
