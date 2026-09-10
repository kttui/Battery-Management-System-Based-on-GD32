#include "gpio.h"

/**
 * @brief 初始化板级GPIO时钟、默认电平和各功能引脚模式。
 * @param 无。
 * @return 无返回值。
 */
void MX_GPIO_Init(void)
{
    
    rcu_periph_clock_enable(RCU_GPIOA);
    rcu_periph_clock_enable(RCU_GPIOB);
    rcu_periph_clock_enable(RCU_GPIOD);
		rcu_periph_clock_enable(RCU_AF);


    /* 鍏堣缃緭鍑洪粯璁ょ數骞筹紝鍐嶅垏鎹?GPIO 妯″紡锛岄伩鍏嶄笂鐢靛垵濮嬪寲鏃朵骇鐢熸瘺鍒恒??*/
    gpio_bit_set(RS485_EN_GPIO_Port, RS485_EN_Pin);
    gpio_bit_set(RS485_PWREN_GPIO_Port, RS485_PWREN_Pin);
    gpio_bit_set(CAN_PWREN_GPIO_Port, CAN_PWREN_Pin);
    gpio_bit_set(I2C1_SDA_GPIO_Port, I2C1_SDA_Pin);
    gpio_bit_set(I2C1_SCL_GPIO_Port, I2C1_SCL_Pin);
    gpio_bit_set(LED_GPIO_Port, LED_Pin);
    gpio_bit_set(LED1_GPIO_Port, LED1_Pin);
    gpio_bit_set(LED2_GPIO_Port, LED2_Pin);
    gpio_bit_set(LED3_GPIO_Port, LED3_Pin);
    gpio_bit_set(LED4_GPIO_Port, LED4_Pin);
    gpio_bit_reset(GD30BM2016_RST_SHUT_GPIO_Port, GD30BM2016_RST_SHUT_Pin);

    /* PA4: RS485_EN锛孯S485 鏀跺彂鏂瑰悜鎺у埗锛屼綆鐢靛钩鎺ユ敹锛岄珮鐢靛钩鍙戦?併??*/
    gpio_init(RS485_EN_GPIO_Port, GPIO_MODE_OUT_PP, GPIO_OSPEED_50MHZ, RS485_EN_Pin);

    /* PA5: RS485_PWREN锛孯S485 鏀跺彂鍣ㄧ數婧愪娇鑳斤紝褰撳墠鎸変綆鐢靛钩鏈夋晥鎺у埗銆?*/
    gpio_init(RS485_PWREN_GPIO_Port, GPIO_MODE_OUT_PP, GPIO_OSPEED_50MHZ, RS485_PWREN_Pin);

    /* PA7: mode key, active low with internal pull-up. */
    gpio_init(KEY_MODE_GPIO_Port, GPIO_MODE_IPU, GPIO_OSPEED_50MHZ, KEY_MODE_Pin);

    /* PA15: BQ769X0_TS1锛屽墠绔姱鐗囧敜閱掕剼锛屽敜閱掑悗椹卞姩浼氬垏鍥炶緭鍏ラ伩鍏嶅奖鍝嶆俯搴﹂噰鏍枫??*/

    /* PB10: CAN_PWREN锛孋AN 鏀跺彂鍣ㄧ數婧愪娇鑳斤紝寮?婕忚緭鍑猴紝褰撳墠鎸変綆鐢靛钩鏈夋晥鎺у埗銆?*/
    gpio_init(CAN_PWREN_GPIO_Port, GPIO_MODE_OUT_OD, GPIO_OSPEED_50MHZ, CAN_PWREN_Pin);

    /* PB12: GD30BM2016 RST_SHUT, output low for normal operation. */
    gpio_init(GD30BM2016_RST_SHUT_GPIO_Port, GPIO_MODE_OUT_PP, GPIO_OSPEED_50MHZ, GD30BM2016_RST_SHUT_Pin);

    /* PB13: I2C1_SDA锛岃蒋浠?I2C 鏁版嵁绾匡紝杞?I2C 椹卞姩鍒濆鍖栧悗浼氬垏涓哄紑婕忔ā寮忋??*/
    gpio_init(I2C1_SDA_GPIO_Port, GPIO_MODE_OUT_PP, GPIO_OSPEED_50MHZ, I2C1_SDA_Pin);

    /* PB14: I2C1_SCL锛岃蒋浠?I2C 鏃堕挓绾匡紝杞?I2C 椹卞姩鍒濆鍖栧悗浼氬垏涓哄紑婕忔ā寮忋??*/
    gpio_init(I2C1_SCL_GPIO_Port, GPIO_MODE_OUT_PP, GPIO_OSPEED_50MHZ, I2C1_SCL_Pin);

    /* PB5: LED锛屾澘杞借繍琛岀姸鎬佹寚绀虹伅锛屼綆鐢靛钩鐐逛寒銆?*/
    gpio_init(LED_GPIO_Port, GPIO_MODE_OUT_PP, GPIO_OSPEED_50MHZ, LED_Pin);

    /* PB6: LED1锛孲OC 鐢甸噺鎸囩ず鐏?1锛屼綆鐢靛钩鐐逛寒銆?*/
    gpio_init(LED1_GPIO_Port, GPIO_MODE_OUT_PP, GPIO_OSPEED_50MHZ, LED1_Pin);

    /* PB7: LED2锛孲OC 鐢甸噺鎸囩ず鐏?2锛屼綆鐢靛钩鐐逛寒銆?*/
    gpio_init(LED2_GPIO_Port, GPIO_MODE_OUT_PP, GPIO_OSPEED_50MHZ, LED2_Pin);

    /* PB8: LED3锛孲OC 鐢甸噺鎸囩ず鐏?3锛屼綆鐢靛钩鐐逛寒銆?*/
    gpio_init(LED3_GPIO_Port, GPIO_MODE_OUT_PP, GPIO_OSPEED_50MHZ, LED3_Pin);

    /* PB9: LED4锛孲OC 鐢甸噺鎸囩ず鐏?4锛屼綆鐢靛钩鐐逛寒銆?*/
    gpio_init(LED4_GPIO_Port, GPIO_MODE_OUT_PP, GPIO_OSPEED_50MHZ, LED4_Pin);
		/* PA15 榛樿鍙兘琚?JTAG 鍗犵敤锛涘叧闂?JTAG銆佷繚鐣?SWD锛岄噴鏀?PA15 缁?BQ769x0_TS1 浣跨敤銆?*/
    gpio_pin_remap_config(GPIO_SWJ_SWDPENABLE_REMAP, ENABLE);
}
