#ifndef __USART_H__
#define __USART_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"

void MX_USART1_UART_Init(void);
void MX_USART2_UART_Init(void);
void print_usart1(const char *format, ...);
void USART2_StartReceive(void);

#ifdef __cplusplus
}
#endif

#endif
