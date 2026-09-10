#include "can.h"

/**
 * @brief 初始化CAN0控制器、收发引脚和500 kbit/s位时序。
 * @param 无。
 * @return 无返回值。
 */
void MX_CAN_Init(void)
{
    can_parameter_struct can_parameter;

    rcu_periph_clock_enable(RCU_AF);
    rcu_periph_clock_enable(RCU_GPIOA);
    rcu_periph_clock_enable(RCU_CAN0);

    /* PA11: CAN0_RX，CAN 控制器接收脚，使用 GD32C11x 默认映射。 */
    gpio_init(CAN_RX_GPIO_Port, GPIO_MODE_IN_FLOATING, GPIO_OSPEED_50MHZ, CAN_RX_Pin);

    /* PA12: CAN0_TX，CAN 控制器发送脚，使用 GD32C11x 默认映射。 */
    gpio_init(CAN_TX_GPIO_Port, GPIO_MODE_AF_PP, GPIO_OSPEED_50MHZ, CAN_TX_Pin);

    can_deinit(BMS_CAN_PERIPH);
    can_struct_para_init(CAN_INIT_STRUCT, &can_parameter);

    can_parameter.working_mode = CAN_NORMAL_MODE;
    can_parameter.time_triggered = DISABLE;
    can_parameter.auto_bus_off_recovery = DISABLE;
    can_parameter.auto_wake_up = DISABLE;
    can_parameter.auto_retrans = ENABLE;
    can_parameter.rec_fifo_overwrite = DISABLE;
    can_parameter.trans_fifo_order = DISABLE;
    can_parameter.resync_jump_width = CAN_BT_SJW_1TQ;
    /* APB1=60MHz, 60MHz / prescaler 6 / (1+15+4)TQ = 500kbit/s. */
    can_parameter.time_segment_1 = CAN_BT_BS1_15TQ;
    can_parameter.time_segment_2 = CAN_BT_BS2_4TQ;
    can_parameter.prescaler = 6U;

    if (ERROR == can_init(BMS_CAN_PERIPH, &can_parameter)) {
        Error_Handler();
    }
}
