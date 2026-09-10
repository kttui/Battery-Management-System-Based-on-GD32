#include "main.h"
#include "systick.h"
#include "gpio.h"
#include "usart.h"
#include "can.h"
#include "cmsis_os2.h"

#include "bms_app.h"
#include "drv_softi2c_gd30bm2016.h"

static void BMS_StartTask(void *argument);

static const osThreadAttr_t bmsStartTask_attributes = {
    .name = "bmsStartTask",
    .stack_size = 1536U,
    .priority = (osPriority_t)osPriorityHigh,
};

/**
 * @brief 完成系统入口初始化，创建BMS启动任务并启动RTOS调度器。
 * @param 无。
 * @return 正常情况下不返回；返回0表示正常结束。
 */
int main(void)
{
    systick_config();

    MX_GPIO_Init();
    MX_USART1_UART_Init();
		
    print_usart1("\r\nGD32C11x BMS start\r\n");
    MX_USART2_UART_Init();
    print_usart1("BOOT: USART2 OK\r\n");

    MX_CAN_Init();
    print_usart1("BOOT: CAN OK\r\n");

    print_usart1("AFE: GD30BM2016, cells: 5S(VC0-VC4+VC16), Rsns: 1.333mOhm\r\n");

    if (osKernelInitialize() != osOK) {
        print_usart1("BOOT: osKernelInitialize failed\r\n");
        Error_Handler();
    }
    print_usart1("BOOT: kernel initialized\r\n");

    if (osThreadNew(BMS_StartTask, NULL, &bmsStartTask_attributes) == NULL) {
        print_usart1("BOOT: create BMS task failed\r\n");
        Error_Handler();
    }
    print_usart1("BOOT: BMS task created\r\n");

    if (osKernelStart() != osOK) {
        print_usart1("BOOT: osKernelStart failed\r\n");
        Error_Handler();
    }

    Error_Handler();
}

/**
 * @brief 执行一次性的BMS系统初始化，初始化完成后退出启动任务。
 * @param argument 任务入口参数，本函数未使用。
 * @return 无返回值。
 */
static void BMS_StartTask(void *argument)
{
    (void)argument;

    print_usart1("BMS initialize...\r\n");
    if (BMS_SysInitialize() == false) {
        print_usart1("BMS initialize failed\r\n");
        Error_Handler();
    }
    print_usart1("BMS started\r\n");

    osThreadExit();
}

/**
 * @brief 处理不可恢复错误，关闭中断并使系统停留在安全死循环。
 * @param 无。
 * @return 无返回值。
 */
void Error_Handler(void)
{
    __disable_irq();
    while (1) {
    }
}
