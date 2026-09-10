#include "usart.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define DEBUG_USART_PERIPH USART0
#define RS485_USART_PERIPH USART1
#define USART_TX_TIMEOUT 3000000U

static volatile uint8_t usart_rs485_rx_byte;

/**
 * @brief 配置指定USART的波特率、帧格式及收发功能。
 * @param usart_periph USART外设编号。
 * @param baudrate 目标波特率。
 * @return 无返回值。
 */
static void usart_common_init(uint32_t usart_periph, uint32_t baudrate)
{
    usart_deinit(usart_periph);
    usart_baudrate_set(usart_periph, baudrate);
    usart_word_length_set(usart_periph, USART_WL_8BIT);
    usart_stop_bit_set(usart_periph, USART_STB_1BIT);
    usart_parity_config(usart_periph, USART_PM_NONE);
    usart_hardware_flow_rts_config(usart_periph, USART_RTS_DISABLE);
    usart_hardware_flow_cts_config(usart_periph, USART_CTS_DISABLE);
    usart_transmit_config(usart_periph, USART_TRANSMIT_ENABLE);
    usart_receive_config(usart_periph, USART_RECEIVE_ENABLE);
    usart_enable(usart_periph);
}

/**
 * @brief 初始化USART0调试串口及对应GPIO。
 * @param 无。
 * @return 无返回值。
 */
void MX_USART1_UART_Init(void)
{
    rcu_periph_clock_enable(RCU_AF);
    rcu_periph_clock_enable(RCU_GPIOA);
    rcu_periph_clock_enable(RCU_USART0);

    /* PA9: USART0_TX，调试日志串口发送脚。 */
    gpio_init(DEBUG_USART_TX_GPIO_Port, GPIO_MODE_AF_PP, GPIO_OSPEED_50MHZ, DEBUG_USART_TX_Pin);

    /* PA10: USART0_RX，调试日志串口接收脚，当前作为预留输入。 */
    gpio_init(DEBUG_USART_RX_GPIO_Port, GPIO_MODE_IN_FLOATING, GPIO_OSPEED_50MHZ, DEBUG_USART_RX_Pin);

    usart_common_init(DEBUG_USART_PERIPH, 115200U);
}

/**
 * @brief 初始化USART1 RS485串口、接收中断及对应GPIO。
 * @param 无。
 * @return 无返回值。
 */
void MX_USART2_UART_Init(void)
{
    rcu_periph_clock_enable(RCU_AF);
    rcu_periph_clock_enable(RCU_GPIOA);
    rcu_periph_clock_enable(RCU_USART1);

    /* PA2: USART1_TX，RS485 数据发送脚。 */
    gpio_init(RS485_USART_TX_GPIO_Port, GPIO_MODE_AF_PP, GPIO_OSPEED_50MHZ, RS485_USART_TX_Pin);

    /* PA3: USART1_RX，RS485 数据接收脚。 */
    gpio_init(RS485_USART_RX_GPIO_Port, GPIO_MODE_IN_FLOATING, GPIO_OSPEED_50MHZ, RS485_USART_RX_Pin);

    usart_common_init(RS485_USART_PERIPH, 115200U);
    USART2_StartReceive();
    nvic_irq_enable(USART1_IRQn, 5U, 0U);
}

/**
 * @brief 以轮询方式向指定USART发送一段字节数据。
 * @param usart_periph USART外设编号。
 * @param data 待发送的只读字节缓冲区。
 * @param length 数据长度。
 * @return 无返回值。
 */
static void usart_send_bytes(uint32_t usart_periph, const uint8_t *data, uint32_t length)
{
    while (length > 0U) {
        uint32_t timeout = USART_TX_TIMEOUT;

        while (RESET == usart_flag_get(usart_periph, USART_FLAG_TBE)) {
            if (timeout-- == 0U) {
                return;
            }
        }

        usart_flag_clear(usart_periph, USART_FLAG_TC);
        usart_data_transmit(usart_periph, *data++);
        length--;

        timeout = USART_TX_TIMEOUT;
        while (RESET == usart_flag_get(usart_periph, USART_FLAG_TC)) {
            if (timeout-- == 0U) {
                return;
            }
        }
    }
}

/**
 * @brief 格式化并通过调试USART输出日志。
 * @param format 格式化字符串。
 * @param ... 可变格式化参数。
 * @return 无返回值。
 */
void print_usart1(const char *format, ...)
{
    char buf[128];

    va_list ap;
    int len;
		__disable_irq();
    va_start(ap, format);
    len = vsnprintf(buf, sizeof(buf), format, ap);
    
		va_end(ap);
    if (len > 0) {
        if ((uint32_t)len >= sizeof(buf)) {
            len = sizeof(buf) - 1U;
        }
        usart_send_bytes(DEBUG_USART_PERIPH, (const uint8_t *)buf, (uint32_t)len);
    }

		__enable_irq();
}

/**
 * @brief 使能RS485串口接收缓冲区非空中断。
 * @param 无。
 * @return 无返回值。
 */
void USART2_StartReceive(void)
{
    usart_interrupt_enable(RS485_USART_PERIPH, USART_INT_RBNE);
}

/**
 * @brief 处理RS485串口接收中断并回显接收字节。
 * @param 无。
 * @return 无返回值。
 */
void USART1_IRQHandler(void)
{
    if (SET == usart_interrupt_flag_get(RS485_USART_PERIPH, USART_INT_FLAG_RBNE)) {
        usart_rs485_rx_byte = (uint8_t)usart_data_receive(RS485_USART_PERIPH);
        usart_data_transmit(RS485_USART_PERIPH, usart_rs485_rx_byte);
    }
}
