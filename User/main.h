#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

#include "gd32c11x.h"
#include "freertos.h"
#include "task.h"
#include "cmsis_os2.h"
void Error_Handler(void);

/* PA2: USART1_TX，RS485 数据发送脚，连�?RS485 收发�?DI�?*/
#define RS485_USART_TX_Pin       GPIO_PIN_2
#define RS485_USART_TX_GPIO_Port GPIOA

/* PA3: USART1_RX，RS485 数据接收脚，连接 RS485 收发�?RO�?*/
#define RS485_USART_RX_Pin       GPIO_PIN_3
#define RS485_USART_RX_GPIO_Port GPIOA

/* PA4: RS485 收发器方向控制脚，低电平为接收，高电平为发送�?*/
#define RS485_EN_Pin             GPIO_PIN_4
#define RS485_EN_GPIO_Port       GPIOA

/* PA5: RS485 收发器电源使能脚，drv_rs485.c 中按低电平有效处理�?*/
#define RS485_PWREN_Pin          GPIO_PIN_5
#define RS485_PWREN_GPIO_Port    GPIOA

/* PA7: mode key input, active low. */
#define KEY_MODE_Pin             GPIO_PIN_7
#define KEY_MODE_GPIO_Port       GPIOA

/* PA9: USART0_TX，调试日志串口发送脚，用�?print_usart1 输出�?*/
#define DEBUG_USART_TX_Pin       GPIO_PIN_9
#define DEBUG_USART_TX_GPIO_Port GPIOA

/* PA10: USART0_RX，调试日志串口接收脚，当前作为预留输入�?*/
#define DEBUG_USART_RX_Pin       GPIO_PIN_10
#define DEBUG_USART_RX_GPIO_Port GPIOA

/* PA11: CAN0_RX，CAN 控制器接收脚，连�?CAN 收发�?RXD�?*/
#define CAN_RX_Pin               GPIO_PIN_11
#define CAN_RX_GPIO_Port         GPIOA

/* PA12: CAN0_TX，CAN 控制器发送脚，连�?CAN 收发�?TXD�?*/
#define CAN_TX_Pin               GPIO_PIN_12
#define CAN_TX_GPIO_Port         GPIOA

/* PA15: BQ769x0 �?TS1 引脚，用于唤醒前端芯片，唤醒后释放给温度采样使用�?*/

/* PB5: 板载运行状态指示灯，低电平点亮�?*/
#define LED_Pin                  GPIO_PIN_5
#define LED_GPIO_Port            GPIOB

/* PB6: SOC 电量指示�?LED1，低电平点亮�?*/
#define LED1_Pin                 GPIO_PIN_15
#define LED1_GPIO_Port           GPIOA

/* PB7: SOC 电量指示�?LED2，低电平点亮�?*/
#define LED2_Pin                 GPIO_PIN_3
#define LED2_GPIO_Port           GPIOB

/* PB8: SOC 电量指示�?LED3，低电平点亮�?*/
#define LED3_Pin                 GPIO_PIN_4
#define LED3_GPIO_Port           GPIOB

/* PB9: SOC 电量指示�?LED4，低电平点亮�?*/
#define LED4_Pin                 GPIO_PIN_5
#define LED4_GPIO_Port           GPIOB

/* PB10: CAN 收发器电源使能脚，drv_can.c 中按低电平有效处理�?*/
#define CAN_PWREN_Pin            GPIO_PIN_10
#define CAN_PWREN_GPIO_Port      GPIOB

/* PB12: GD30BM2016 RST_SHUT, keep low during normal operation. */
#define GD30BM2016_RST_SHUT_Pin        GPIO_PIN_12
#define GD30BM2016_RST_SHUT_GPIO_Port  GPIOB
#define GD30BM2016_ALERT_Pin           0U

/* PB13: 软件 I2C �?SDA 数据线，用于连接 BQ769x0 电池监测芯片�?*/
#define I2C1_SDA_Pin             GPIO_PIN_7
#define I2C1_SDA_GPIO_Port       GPIOB

/* PB14: 软件 I2C �?SCL 时钟线，用于连接 BQ769x0 电池监测芯片�?*/
#define I2C1_SCL_Pin             GPIO_PIN_6
#define I2C1_SCL_GPIO_Port       GPIOB

#ifdef __cplusplus
}
#endif

#endif
