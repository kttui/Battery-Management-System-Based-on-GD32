/*!
    \file    gd32c11x_it.c
    \brief   interrupt service routines

    \version 2026-01-30, V1.6.0, firmware for GD32C11x
*/

#include "gd32c11x_it.h"
#include "main.h"
#include "systick.h"

extern void xPortSysTickHandler(void);

/*!
    \brief      this function handles NMI exception
    \param[in]  none
    \param[out] none
    \retval     none
*/
/**
 * @brief 处理不可屏蔽中断异常。
 * @param 无。
 * @return 无返回值。
 */
void NMI_Handler(void)
{
    while (1) {
    }
}

/*!
    \brief      this function handles HardFault exception
    \param[in]  none
    \param[out] none
    \retval     none
*/
/**
 * @brief 处理 HardFault 硬故障异常。
 * @param 无。
 * @return 无返回值。
 */
void HardFault_Handler(void)
{
    while (1) {
    }
}

/*!
    \brief      this function handles MemManage exception
    \param[in]  none
    \param[out] none
    \retval     none
*/
/**
 * @brief 处理内存管理异常。
 * @param 无。
 * @return 无返回值。
 */
void MemManage_Handler(void)
{
    while (1) {
    }
}

/*!
    \brief      this function handles BusFault exception
    \param[in]  none
    \param[out] none
    \retval     none
*/
/**
 * @brief 处理总线访问异常。
 * @param 无。
 * @return 无返回值。
 */
void BusFault_Handler(void)
{
    while (1) {
    }
}

/*!
    \brief      this function handles UsageFault exception
    \param[in]  none
    \param[out] none
    \retval     none
*/
/**
 * @brief 处理非法指令等用法异常。
 * @param 无。
 * @return 无返回值。
 */
void UsageFault_Handler(void)
{
    while (1) {
    }
}

/*!
    \brief      this function handles DebugMon exception
    \param[in]  none
    \param[out] none
    \retval     none
*/
/**
 * @brief 处理调试监控异常。
 * @param 无。
 * @return 无返回值。
 */
void DebugMon_Handler(void)
{
    while (1) {
    }
}

/*!
    \brief      this function handles SysTick exception
    \param[in]  none
    \param[out] none
    \retval     none
*/
/**
 * @brief 处理系统节拍中断，驱动 FreeRTOS 时基和阻塞延时计数。
 * @param 无。
 * @return 无返回值。
 */
void SysTick_Handler(void)
{
    if (xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED) {
        xPortSysTickHandler();
    }

    delay_decrement();
}

/*!
    \brief      this function handles EXTI lines 10 to 15 interrupts
    \param[in]  none
    \param[out] none
    \retval     none
*/
/**
 * @brief 处理 EXTI10 至 EXTI15 外部中断并清除已识别的中断标志。
 * @param 无。
 * @return 无返回值。
 */
void EXTI10_15_IRQHandler(void)
{
    if (SET == exti_interrupt_flag_get(EXTI_12)) {
        exti_interrupt_flag_clear(EXTI_12);
    }
}
