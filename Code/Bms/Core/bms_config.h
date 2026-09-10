#ifndef __BMS_CONFIG_H__
#define __BMS_CONFIG_H__

#include "bms_hal_config.h"


/***************************** 基础硬件配置 ***********************************/

// 电池串数上限。本工程使用 GD30BM2016 的 5S 接法：VC0~VC4 + VC16。
// 注意：Cell5 对应 GD30 的 VC16 通道，不是普通 VC5。
// BQ76920: 3~5 串；BQ76930: 6~10 串；BQ76940: 9~15 串。
#define BMS_CELL_MAX    5

// 温度采样通道数量。当前板子暂未确认外部 NTC，所以温度经常显示 invalid。
// BQ76920: 1 路；BQ76930: 2 路；BQ76940: 3 路。
#define BMS_TEMP_MAX    1

// 温度有效测量范围，单位：摄氏度。
// 采样温度超出该范围时会被认为无效。
#define BMS_TEMP_MEASURE_MAX    125
#define BMS_TEMP_MEASURE_MIN    -55

// 温度无效值，用于占位显示或通信上报。
#define BMS_TEMP_INVALID_VALUE  255

// 是否把 0.0 摄氏度温度值当作无效值。
// 当前硬件没有确认 NTC 转换，GD30 可能读到 0.0C，所以先按无效处理。
#define BMS_TEMP_ZERO_IS_INVALID    1


/***************************** 电流采样与模式判断 ***********************************/

// 电流方向配置：正电流表示充电，负电流表示放电。
// 如果实测方向相反，把该值改成 -1.0f。
#define BMS_CURRENT_DIRECTION       1.0f

// 电流零点偏移校准，单位：A。
// 例如空载时显示 +0.03A，可设置为 0.03f 抵消零漂。
#define BMS_CURRENT_ZERO_OFFSET_A   0.0f

// 电流比例校准系数。
// 例如实际 1.00A，显示 0.95A，可设置为 1.00/0.95。
#define BMS_CURRENT_CAL_GAIN        1.0f

// 电流死区，单位：A。
// 绝对值小于该阈值的电流会被显示为 0A，避免零点抖动导致误判充放电。
#define BMS_CURRENT_DEADBAND_A      0.01f

// 自动零点跟踪只允许在充、放电功率通道均关闭时进行。
// 它用于补偿运行时温漂，不能代替上面的产线固定标定值。
#define BMS_CURRENT_ZERO_AUTO_ENABLE          1
#define BMS_CURRENT_ZERO_AUTO_SAMPLE_COUNT    30U
#define BMS_CURRENT_ZERO_AUTO_MAX_OFFSET_A    0.05f
#define BMS_CURRENT_ZERO_AUTO_MAX_SPAN_A      0.016f
#define BMS_CURRENT_ZERO_AUTO_FILTER_GAIN     0.25f

// 系统模式判断阈值。
// >= +阈值：进入充电模式；<= -阈值：进入放电模式；中间为待机模式。
#define BMS_CURRENT_MODE_THRESHOLD_A    BMS_CURRENT_DEADBAND_A


/***************************** 电压采样与校准 ***********************************/

// 单体电压有效范围，单位：V。
// 低于下限或高于上限的单体采样会被置为 0.000V 显示。
#define BMS_CELL_VALID_VOLT_MIN     1.0f
#define BMS_CELL_VALID_VOLT_MAX     5.0f

// 单体电压全局校准：校准后电压 = 原始电压 * GAIN + OFFSET + 单串 OFFSET。
// 如果全部单体都偏高/偏低，优先改全局 GAIN 或 OFFSET。
#define BMS_CELL_VOLTAGE_CAL_GAIN       1.0f
#define BMS_CELL_VOLTAGE_CAL_OFFSET_V   0.0f

// 每串单体电压独立偏移校准，单位：V。
// 例如 Cell3 比万用表低 0.02V，就把 CELL3_OFFSET 改为 0.02f。
#define BMS_CELL1_VOLTAGE_OFFSET_V      0.0f
#define BMS_CELL2_VOLTAGE_OFFSET_V      0.0f
#define BMS_CELL3_VOLTAGE_OFFSET_V      0.0f
#define BMS_CELL4_VOLTAGE_OFFSET_V      0.0f
#define BMS_CELL5_VOLTAGE_OFFSET_V      0.0f

// 电池包总压校准，单位：V。
// 只影响 Battery Voltage 显示/上报，不直接修正每串单体电压。
#define BMS_BATTERY_VOLTAGE_CAL_GAIN        1.0f
#define BMS_BATTERY_VOLTAGE_CAL_OFFSET_V    0.0f


/***************************** 电池容量与睡眠 ***********************************/

// 电池额定容量，单位：Ah。
// SOC 安时积分会用这个值计算剩余容量；当前电池容量为 1.2Ah。
#define BMS_BATTERY_CAPACITY    1.2

// 待机多长时间后进入睡眠，单位：分钟。
// 当前工程睡眠低功耗流程未完全展开，调试阶段一般保持默认即可。
#define BMS_ENTRY_SLEEP_TIME    60


/***************************** 输出模式选择 ***********************************/

// 输出模式枚举值：
// 0：PA7 按键/低电平变化切换充放电模式。
// 1：只允许充电。
// 2：只允许放电。
// 3：充放电都允许。
// 4：充电和放电都关闭。
#define BMS_OUTPUT_MODE_KEY_SWITCH      0
#define BMS_OUTPUT_MODE_CHARGE_ONLY     1
#define BMS_OUTPUT_MODE_DISCHARGE_ONLY  2
#define BMS_OUTPUT_MODE_BOTH            3
#define BMS_OUTPUT_MODE_OFF             4

#define BMS_KEY_SWITCH_MODE_CHARGE      0
#define BMS_KEY_SWITCH_MODE_OFF         1
#define BMS_KEY_SWITCH_MODE_DISCHARGE   2
#define BMS_KEY_SWITCH_DEFAULT_MODE     BMS_KEY_SWITCH_MODE_CHARGE

// 当前输出模式。当前为按键切换模式。
// 如果要固定放电：改为 BMS_OUTPUT_MODE_DISCHARGE_ONLY。
// 如果要固定充电：改为 BMS_OUTPUT_MODE_CHARGE_ONLY。
// 如果要固定充放电都关闭：改为 BMS_OUTPUT_MODE_OFF。
#define BMS_OUTPUT_MODE                 BMS_OUTPUT_MODE_KEY_SWITCH

// 软件单体过压保护开关。
// 0：调试时临时关闭软件 OV 保护；1：启用软件 OV 保护。
// 注意：关闭这里不代表 AFE 硬件内部所有保护都一定关闭。
#define BMS_OV_PROTECT_ENABLE           1


/***************************** 电池保护相关参数 ***********************************/

// 三元锂电池默认参数，单体满充电压通常为 4.20V。
#define TLB_OV_PROTECT          4.20    // 单体过压保护电压(V)
#define TLB_OV_RELIEVE          4.18    // 单体过压恢复电压(V)
#define TLB_UV_PROTECT          3.10    // 单体欠压保护电压(V)
#define TLB_UV_RELIEVE          3.15    // 单体欠压恢复电压(V)
#define TLB_SHUTDOWN_VOLTAGE    3.08    // 自动关机参考电压(V)
#define TLB_BALANCE_VOLTAGE     3.30    // 均衡允许启动的最低单体电压(V)

// 磷酸铁锂电池默认参数，当前工程未选用。
#define LIPB_OV_PROTECT         3.60    // 单体过压保护电压(V)
#define LIPB_OV_RELIEVE         3.55    // 单体过压恢复电压(V)
#define LIPB_UV_PROTECT         2.60    // 单体欠压保护电压(V)
#define LIPB_UV_RELIEVE         2.65    // 单体欠压恢复电压(V)
#define LIPB_SHUTDOWN_VOLTAGE   2.50    // 自动关机参考电压(V)
#define LIPB_BALANCE_VOLTAGE    3.00    // 均衡允许启动的最低单体电压(V)

// 钛酸锂电池默认参数，当前工程未选用。
#define LTB_OV_PROTECT          2.70    // 单体过压保护电压(V)
#define LTB_OV_RELIEVE          2.65    // 单体过压恢复电压(V)
#define LTB_UV_PROTECT          1.80    // 单体欠压保护电压(V)
#define LTB_UV_RELIEVE          1.85    // 单体欠压恢复电压(V)
#define LTB_SHUTDOWN_VOLTAGE    1.70    // 自动关机参考电压(V)
#define LTB_BALANCE_VOLTAGE     2.30    // 均衡允许启动的最低单体电压(V)

// 当前工程使用的保护参数来源。
// 现在选择三元锂参数，如需换化学体系，可把 TLB_* 改成 LIPB_* 或 LTB_*。
#define INIT_OV_PROTECT         TLB_OV_PROTECT      // 单体过压保护电压(V)
#define INIT_OV_RELIEVE         TLB_OV_RELIEVE      // 单体过压恢复电压(V)
#define INIT_UV_PROTECT         TLB_UV_PROTECT      // 单体欠压保护电压(V)
#define INIT_UV_RELIEVE         TLB_UV_RELIEVE      // 单体欠压恢复电压(V)
#define INIT_SHUTDOWN_VOLTAGE   TLB_SHUTDOWN_VOLTAGE// 自动关机参考电压(V)
#define INIT_BALANCE_VOLTAGE    TLB_BALANCE_VOLTAGE // 均衡允许启动的最低单体电压(V)

// 最大均衡电流，单位：A。
// 当前外部均衡由硬件电阻决定，此值主要作为配置/预留参数。
#define INIT_BALANCE_CURRENT_MAX    0.6

// 最大充电/放电电流，单位：A。
// 当前电池最大电流为 1A，超过阈值会触发 OCC/OCD 软件保护。
#define INIT_OCC_MAX                1.0     // 充电过流保护阈值(A)
#define INIT_OCD_MAX                1.0     // 放电过流保护阈值(A)

// 电压保护延时。
// OV：Over Voltage，过压；UV：Under Voltage，欠压。
#define INIT_OV_DELAY       BMS_OV_DELAY_2s     // 过压持续该时间后触发保护
#define INIT_UV_DELAY       BMS_UV_DELAY_4s     // 欠压持续该时间后触发保护

// 放电过流保护。
// OCD：Over Current Discharge。
#define INIT_OCD_DELAY      BMS_OCD_DELAY_320ms // 放电过流延时
#define INIT_OCD_RELIEVE    60                  // 放电过流解除等待时间(s)

// 放电短路保护。
// SCD：Short Circuit Discharge。
#define INIT_SCD_DELAY      BMS_SCD_DELAY_100us // 短路保护延时
#define INIT_SCD_RELIEVE    60                  // 短路保护解除等待时间(s)

// 充电过流保护。
// OCC：Over Current Charge。
#define INIT_OCC_DELAY      1                   // 充电过流延时(s)
#define INIT_OCC_RELIEVE    60                  // 充电过流解除等待时间(s)

// 温度保护参数，单位：摄氏度。
// 当前温度采样还未确认，日志可能显示 Temperature invalid。
#define INIT_OTC_PROTECT    80      // 充电过温保护阈值
#define INIT_OTC_RELIEVE    50      // 充电过温恢复阈值
#define INIT_OTD_PROTECT    80      // 放电过温保护阈值
#define INIT_OTD_RELIEVE    50      // 放电过温恢复阈值
#define INIT_LTC_PROTECT    -20     // 充电低温保护阈值
#define INIT_LTC_RELIEVE    -10     // 充电低温恢复阈值
#define INIT_LTD_PROTECT    -20     // 放电低温保护阈值
#define INIT_LTD_RELIEVE    -10     // 放电低温恢复阈值


/***************************** SOC 估算参数 ***********************************/

// SOC 充放电控制阈值。
// SOC_STOP_CHG_VALUE=1 表示 SOC 到 100% 时停止充电。
// SOC_STOP_DSG_VALUE=0 表示 SOC 到 0% 时停止放电。
#define SOC_STOP_CHG_VALUE      1
#define SOC_START_CHG_VALUE     0.99
#define SOC_STOP_DSG_VALUE      0
#define SOC_START_DSG_VALUE     0.01

// SOC 估算方式：上电 OCV 初始化 + 运行中安时积分 + 静置时 OCV 慢校准。
#define SOC_EST_OCV_FUSION_ENABLE       1       // 是否启用静置 OCV 融合校准
#define SOC_EST_REST_CURRENT_A          0.03f   // 小于该电流认为处于静置状态(A)
#define SOC_EST_REST_TIME_MS            300000U // 静置多久后允许 OCV 校准(ms)，当前 5 分钟
#define SOC_EST_OCV_CORRECT_INTERVAL_MS 30000U  // OCV 校准间隔(ms)
#define SOC_EST_OCV_CORRECT_GAIN        0.05f   // 每次按误差的 5% 修正 SOC
#define SOC_EST_OCV_MAX_STEP            0.005f  // 单次最大修正 0.5% SOC
#define SOC_EST_CHARGE_EFFICIENCY       0.995f  // 充电库仑效率
#define SOC_EST_DISCHARGE_EFFICIENCY    1.000f  // 放电库仑效率
#define SOC_EST_AH_MAX_DT_MS            5000U   // 安时积分最大时间步长，避免任务阻塞后积分过大

// 静置必须同时满足待机/休眠模式以及最低单体电压稳定，避免把真实小电流负载误判成静置。
#define SOC_EST_REST_REQUIRE_STANDBY_MODE     1
#define SOC_EST_REST_VOLTAGE_WINDOW_MS        30000U
#define SOC_EST_REST_VOLTAGE_MAX_DELTA_V      0.005f

// SOC持久化占用GD32C113内部Flash最后两个1KB页，工程链接空间已同步缩短。
#define SOC_EST_PERSIST_ENABLE                1
#define SOC_EST_FLASH_PAGE_SIZE               0x400U
#define SOC_EST_FLASH_PAGE0_ADDRESS           0x0801F800U
#define SOC_EST_FLASH_PAGE1_ADDRESS           0x0801FC00U
#define SOC_EST_PERSIST_FIRST_SAVE_MS          60000U
#define SOC_EST_PERSIST_MIN_INTERVAL_MS        600000U
#define SOC_EST_PERSIST_FORCE_INTERVAL_MS      21600000U
#define SOC_EST_PERSIST_SOC_DELTA              0.01f
#define SOC_EST_RESTORE_MAX_OCV_DIFF           0.30f

// 容量/SOH通过两次可信静置OCV锚点之间的净安时量进行学习。
#define SOC_EST_CAPACITY_LEARN_ENABLE          1
#define SOC_EST_CAPACITY_LEARN_MIN_SOC_SPAN    0.40f
#define SOC_EST_CAPACITY_LEARN_MIN_NET_RATIO   0.90f
#define SOC_EST_CAPACITY_LEARN_FILTER_GAIN     0.10f
#define SOC_EST_CAPACITY_MIN_RATIO             0.50f
#define SOC_EST_CAPACITY_MAX_RATIO             1.10f


/***************************** 预充/预放参数 ***********************************/

// 当前项目暂不使用预充/预放，所以阈值和时间均设为 0。
#define PRECHARGE_START_VOLTAGE     0.00f   // 低于该单体电压时使用预充 MOS
#define PRECHARGE_STOP_VOLTAGE      0.00f   // 高于该单体电压后切换到正常充电 MOS
#define PREDISCHARGE_TIME_MS        0U      // 预放持续时间(ms)，之后切换到正常放电 MOS


/***************************** 外部均衡参数 ***********************************/

// 均衡触发条件：最高单体与最低单体差值超过该阈值时，选择高电压单体均衡。
#define BALANCE_DIFFE_VOLTAGE       0.05    // 均衡差异电压(V)

// 单次均衡开启周期。
// 到时后会关闭均衡，等待电压回升延时后再次判断。
#define BALANCE_CYCLE_TIME          30      // 均衡周期时间(s)
#define BALANCE_VOLT_RISE_DELAY     5000    // 均衡关闭后的电压回升等待时间(ms)

// 上电默认是否允许均衡。
#define BMS_BALANCE_DEFAULT_ENABLE      1U

// 是否启用 GD30BM2016 外部均衡控制。
#define EXTERNAL_BALANCE_ENABLE         1U

// 同一轮最多允许同时均衡的单体数量。
#define EXTERNAL_BALANCE_MAX_CELLS      3U

// 写入 AFE 的均衡周期配置。
#define EXTERNAL_BALANCE_AFE_INTERVAL_SEC   BALANCE_CYCLE_TIME

// 重新触发 GD30 均衡 active 位前的等待时间。
#define EXTERNAL_BALANCE_RETRIGGER_DELAY_MS 500U

// 均衡灯/均衡通道上电自检。
// 1：启动时依次打开 Cell1~Cell5 均衡通道，便于观察 LED 或测量 100Ω 电阻两端电压。
// 0：关闭自检，正常量产/长期运行建议关闭。
#define BALANCE_LED_SELF_TEST_ENABLE     0U
#define BALANCE_LED_SELF_TEST_CELL_MS    20000U  // 每串自检保持时间(ms)
#define BALANCE_LED_SELF_TEST_MIN_CELL_V 3.00f  // 最低单体低于该值时跳过自检，避免继续放电


/***************************** CAN 通信帧 ID ***********************************/

// 基础 CAN 遥测标准帧 ID。
#define BMS_CAN_ID_STATUS      0x500U  // BMS 状态帧
#define BMS_CAN_ID_CELL_1_4    0x501U  // Cell1~Cell4 电压帧
#define BMS_CAN_ID_CELL_5_8    0x502U  // Cell5~Cell8 电压帧，当前 5S 只使用 Cell5

/*************************************************************************************/

#endif
