#define BMS_DBG_TAG "Charger"

#include "bms_charger.h"

#include <string.h>

#include "cmsis_os2.h"

#include "can.h"
#include "drv_can.h"

#include "bms_analysis.h"
#include "bms_debug.h"
#include "bms_global.h"
#include "bms_hal_control.h"
#include "bms_monitor.h"
#include "bms_protect.h"

#define CHARGER_TASK_STACK_SIZE              768U
#define CHARGER_TASK_PERIOD_MS              100U

#define CHARGER_CAN_ID_CONTROL              0x320U
#define CHARGER_CAN_ID_TELEMETRY            0x321U
#define CHARGER_CAN_ID_STATUS               0x322U
#define CHARGER_CAN_FILTER_TELEMETRY        0U
#define CHARGER_CAN_FILTER_STATUS           1U
#define CHARGER_CAN_STD_ID_MASK_EXACT       0x7FFU

#define CHARGER_PROTOCOL_VERSION            1U
#define CHARGER_DEFAULT_TARGET_MV           18000U//调输出电压
#define CHARGER_DEFAULT_CURRENT_MA          500U
#define CHARGER_DEFAULT_ENABLE              1U
#define CHARGER_DEFAULT_FAULT_CLEAR         1U
#define CHARGER_DEFAULT_ALLOW_BUCK          1U
#define CHARGER_DEFAULT_ALLOW_BOOST         1U
#define CHARGER_WAIT_ONLINE_BEFORE_ENABLE   0U

#define CHARGER_CAN_TX_DEBUG_PRINT          1U
#define BMS_CAN_BUZZER_TEST_ENABLE          1U

#define CHG_FLAG_ENABLE                     0x01U
#define CHG_FLAG_FAULT_CLEAR                0x02U
#define CHG_FLAG_EMERGENCY_STOP             0x04U
#define CHG_FLAG_ALLOW_BOOST                0x08U
#define CHG_FLAG_ALLOW_BUCK                 0x10U
#define CHG_FLAG_BUZZER_TEST                0x20U

#define CHARGER_STATUS_FAULT                3U

static const osThreadAttr_t chargerTask_attributes = {
	.name = "chargerTask",
	.stack_size = CHARGER_TASK_STACK_SIZE,
	.priority = (osPriority_t)osPriorityNormal1,
};

BMS_ChargerDataTypedef BMS_ChargerData;

static void BMS_ChargerTaskEntry(void *parameter);
static void BMS_ChargerProcessRx(void);
static void BMS_ChargerProcessFrame(const CAN_RxFrameTypedef *frame);
static void BMS_ChargerParseTelemetry(const uint8_t *data);
static void BMS_ChargerParseStatus(const uint8_t *data);
static void BMS_ChargerUpdateState(void);
static void BMS_ChargerSendControl(void);
static void BMS_ChargerPrintTxFrame(const uint8_t *data);
static void BMS_ChargerPutU16(uint8_t *data, uint8_t offset, uint16_t value);
static uint16_t BMS_ChargerGetU16(const uint8_t *data, uint8_t offset);
static uint16_t BMS_ChargerScaleToU16(uint32_t value, uint32_t unit);
static bool BMS_ChargerOnline(void);
static bool BMS_ChargerBatteryAllowsCharge(void);
static void BMS_ChargerEnterFaultStop(void);
static void BMS_ChargerStopLocalCharge(void);
static void BMS_ChargerRestoreLocalCharge(void);

static bool BMS_ChargerChargeBackupValid = false;
static BMS_StateTypedef BMS_ChargerChargeBackup = BMS_STATE_DISABLE;
static uint8_t g_charger_buzzer_test = 1U;

/**
 * @brief 初始化充电器协议状态、CAN过滤器并创建充电器任务。
 * @param 无。
 * @return 无返回值。
 */
void BMS_ChargerInit(void)
{
	osThreadId_t thread;

	memset(&BMS_ChargerData, 0, sizeof(BMS_ChargerData));
	BMS_ChargerData.State = BMS_CHARGER_STATE_INIT;
	BMS_ChargerData.TargetVoltageMv = CHARGER_DEFAULT_TARGET_MV;
	BMS_ChargerData.CurrentLimitMa = CHARGER_DEFAULT_CURRENT_MA;
	BMS_ChargerData.ChargeEnableRequest = (CHARGER_DEFAULT_ENABLE != 0U);
	BMS_ChargerData.AllowBuck = (CHARGER_DEFAULT_ALLOW_BUCK != 0U);
	BMS_ChargerData.AllowBoost = (CHARGER_DEFAULT_ALLOW_BOOST != 0U);

	CAN_Initialize();
	CAN_ConfigStdFilter(CHARGER_CAN_FILTER_TELEMETRY,
	                    CHARGER_CAN_ID_TELEMETRY,
	                    CHARGER_CAN_STD_ID_MASK_EXACT);
	CAN_ConfigStdFilter(CHARGER_CAN_FILTER_STATUS,
	                    CHARGER_CAN_ID_STATUS,
	                    CHARGER_CAN_STD_ID_MASK_EXACT);

	thread = osThreadNew(BMS_ChargerTaskEntry, NULL, &chargerTask_attributes);
	if (thread == NULL) {
		BMS_ERROR("Create Task Fail");
	}
}

/**
 * @brief 设置发送给充电器的目标电压和电流限制。
 * @param target_mv 目标充电电压，单位为毫伏。
 * @param current_ma 充电电流限制，单位为毫安。
 * @return 无返回值。
 */
void BMS_ChargerSetTarget(uint32_t target_mv, uint32_t current_ma)
{
	BMS_ChargerData.TargetVoltageMv = target_mv;
	BMS_ChargerData.CurrentLimitMa = current_ma;
}

/**
 * @brief 设置充电请求并同步本地充电许可状态。
 * @param enable 目标使能状态。
 * @return 无返回值。
 */
void BMS_ChargerSetChargeEnable(bool enable)
{
	BMS_ChargerData.ChargeEnableRequest = enable;
	if (enable == false) {
		BMS_ChargerData.State = BMS_CHARGER_STATE_STOP;
		BMS_ChargerStopLocalCharge();
	} else {
		BMS_ChargerRestoreLocalCharge();
	}
}

/**
 * @brief 请求清除充电器故障并撤销急停状态。
 * @param 无。
 * @return 无返回值。
 */
void BMS_ChargerClearFault(void)
{
	BMS_ChargerData.FaultClearPending = true;
	BMS_ChargerData.EmergencyStop = false;
}

/**
 * @brief 触发充电器急停并关闭本地充电通路。
 * @param 无。
 * @return 无返回值。
 */
void BMS_ChargerEmergencyStop(void)
{
	BMS_ChargerData.EmergencyStop = true;
	BMS_ChargerData.FaultStop = true;
	BMS_ChargerData.State = BMS_CHARGER_STATE_FAULT;
	BMS_ChargerStopLocalCharge();
}

/**
 * @brief 设置充电器蜂鸣器测试请求。
 * @param on 目标开关状态。
 * @return 无返回值。
 */
void bms_charger_buzzer_test_set(uint8_t on)
{
	g_charger_buzzer_test = on ? 1U : 0U;
}

/**
 * @brief 运行充电器任务，周期收帧、更新状态并发送控制帧。
 * @param parameter 任务入口参数，本函数未使用。
 * @return 无返回值。
 */
static void BMS_ChargerTaskEntry(void *parameter)
{
	(void)parameter;

	while (1) {
		BMS_ChargerProcessRx();
		BMS_ChargerUpdateState();
		BMS_ChargerSendControl();
		osDelay(CHARGER_TASK_PERIOD_MS);
	}
}

/**
 * @brief 从CAN FIFO批量读取并处理充电器反馈帧。
 * @param 无。
 * @return 无返回值。
 */
static void BMS_ChargerProcessRx(void)
{
	CAN_RxFrameTypedef frame;
	uint8_t frame_count = 0U;

	while (CAN_ReceiveData(&frame) == true) {
		BMS_ChargerProcessFrame(&frame);
		frame_count++;
		if (frame_count >= 8U) {
			break;
		}
	}
}

/**
 * @brief 校验CAN帧格式并按帧ID分派解析。
 * @param frame 从 CAN FIFO 读取到的充电器反馈帧。
 * @return 无返回值。
 */
static void BMS_ChargerProcessFrame(const CAN_RxFrameTypedef *frame)
{
	if (frame->IdType != CAN_ID_STD ||
	    frame->IsDataFrame == 0U ||
	    frame->IsClassicFrame == 0U ||
	    frame->Length != 8U) {
		return;
	}

	if (frame->StdId == CHARGER_CAN_ID_TELEMETRY) {
		BMS_ChargerParseTelemetry(frame->Data);
	} else if (frame->StdId == CHARGER_CAN_ID_STATUS) {
		BMS_ChargerParseStatus(frame->Data);
	}
}

/**
 * @brief 解析充电器输入输出电压和电流遥测帧。
 * @param data 8 字节充电器遥测帧的数据区。
 * @return 无返回值。
 */
static void BMS_ChargerParseTelemetry(const uint8_t *data)
{
	BMS_ChargerData.Telemetry.VinMv = (uint32_t)BMS_ChargerGetU16(data, 0U) * 10U;
	BMS_ChargerData.Telemetry.IinMa = (uint32_t)BMS_ChargerGetU16(data, 2U) * 10U;
	BMS_ChargerData.Telemetry.VoutMv = (uint32_t)BMS_ChargerGetU16(data, 4U) * 10U;
	BMS_ChargerData.Telemetry.IoutMa = (uint32_t)BMS_ChargerGetU16(data, 6U) * 10U;
	BMS_ChargerData.Telemetry.LastRxTick = osKernelGetTickCount();
	BMS_ChargerData.Telemetry.Valid = true;
}

/**
 * @brief 解析充电器状态、温度、模式和故障帧。
 * @param data 8 字节充电器状态帧的数据区。
 * @return 无返回值。
 */
static void BMS_ChargerParseStatus(const uint8_t *data)
{
	uint8_t mode_byte = data[6];
	uint8_t duty_byte = data[7];

	BMS_ChargerData.Status.LatchedFault = BMS_ChargerGetU16(data, 0U);
	BMS_ChargerData.Status.PresentFault = BMS_ChargerGetU16(data, 2U);
	BMS_ChargerData.Status.ChargerStatus = data[4];
	BMS_ChargerData.Status.TemperatureDegC = (int16_t)data[5];
	BMS_ChargerData.Status.PowerMode = mode_byte & 0x0FU;
	BMS_ChargerData.Status.OutputActive = ((mode_byte & 0x10U) != 0U);
	BMS_ChargerData.Status.BmsOnline = ((mode_byte & 0x20U) != 0U);
	BMS_ChargerData.Status.BuckDuty10Pct = duty_byte & 0x0FU;
	BMS_ChargerData.Status.BoostDuty10Pct = (duty_byte >> 4U) & 0x0FU;
	BMS_ChargerData.Status.LastRxTick = osKernelGetTickCount();
	BMS_ChargerData.Status.Valid = true;

	if (BMS_ChargerData.Status.ChargerStatus == CHARGER_STATUS_FAULT ||
	    BMS_ChargerData.Status.LatchedFault != 0U) {
		BMS_ChargerEnterFaultStop();
	} else if (BMS_ChargerData.FaultStop == true &&
	           BMS_ChargerData.EmergencyStop == false) {
		BMS_ChargerData.FaultStop = false;
		BMS_ChargerRestoreLocalCharge();
		BMS_INFO("Fault cleared");
	}
}

/**
 * @brief 根据请求、在线状态、电池许可和故障更新充电器状态机。
 * @param 无。
 * @return 无返回值。
 */
static void BMS_ChargerUpdateState(void)
{
	if (BMS_ChargerData.EmergencyStop == true || BMS_ChargerData.FaultStop == true) {
		BMS_ChargerData.State = BMS_CHARGER_STATE_FAULT;
		BMS_ChargerStopLocalCharge();
		return;
	}

	if (BMS_ChargerData.ChargeEnableRequest == false) {
		BMS_ChargerData.State = BMS_CHARGER_STATE_STOP;
		BMS_ChargerStopLocalCharge();
		return;
	}

	if (BMS_ChargerOnline() == false) {
		BMS_ChargerData.State = BMS_CHARGER_STATE_INIT;
#if (CHARGER_WAIT_ONLINE_BEFORE_ENABLE != 0U)
#endif
		return;
	}

	if (BMS_ChargerBatteryAllowsCharge() == false) {
		BMS_ChargerData.State = BMS_CHARGER_STATE_READY;
		return;
	}

	if (BMS_ChargerData.Status.Valid == true &&
	    BMS_ChargerData.Status.ChargerStatus == BMS_CHARGER_STATUS_CHARGING) {
		BMS_ChargerData.State = BMS_CHARGER_STATE_CHARGING;
	} else {
		BMS_ChargerData.State = BMS_CHARGER_STATE_READY;
	}
}

/**
 * @brief 组装并发送充电器使能、目标值和控制标志。
 * @param 无。
 * @return 无返回值。
 */
static void BMS_ChargerSendControl(void)
{
	uint8_t data[8] = {0U};
	uint8_t flags = 0U;
	CAN_TxStatusTypedef tx_status;
	uint16_t target = BMS_ChargerScaleToU16(BMS_ChargerData.TargetVoltageMv, 10U);
	uint16_t current = BMS_ChargerScaleToU16(BMS_ChargerData.CurrentLimitMa, 10U);
	bool can_enable = (BMS_ChargerData.ChargeEnableRequest == true &&
	                   BMS_ChargerData.FaultStop == false &&
	                   BMS_ChargerData.EmergencyStop == false &&
	                   BMS_ChargerData.FaultClearPending == false &&
	                   BMS_ChargerBatteryAllowsCharge() == true);

#if (CHARGER_WAIT_ONLINE_BEFORE_ENABLE != 0U)
	can_enable = (can_enable == true && BMS_ChargerOnline() == true);
#endif

	if (can_enable == true) {
		flags |= CHG_FLAG_ENABLE;
	}
	if (BMS_ChargerData.FaultClearPending == true ||
	    CHARGER_DEFAULT_FAULT_CLEAR != 0U) {
		flags |= CHG_FLAG_FAULT_CLEAR;
	}
	if (BMS_ChargerData.EmergencyStop == true) {
		flags |= CHG_FLAG_EMERGENCY_STOP;
	}
	if (BMS_ChargerData.AllowBoost == true) {
		flags |= CHG_FLAG_ALLOW_BOOST;
	}
	if (BMS_ChargerData.AllowBuck == true) {
		flags |= CHG_FLAG_ALLOW_BUCK;
	}
#if (BMS_CAN_BUZZER_TEST_ENABLE != 0U)
	if (g_charger_buzzer_test != 0U) {
		flags |= CHG_FLAG_BUZZER_TEST;
	}
#endif

	data[0] = flags;
	data[1] = BMS_ChargerData.AliveCounter++;
	BMS_ChargerPutU16(data, 2U, target);
	BMS_ChargerPutU16(data, 4U, current);
	data[6] = CHARGER_PROTOCOL_VERSION;
	data[7] = 0U;

	CAN_SendDataWithStatus(CAN_ID_STD, CHARGER_CAN_ID_CONTROL, 0U, data, sizeof(data), &tx_status);
	BMS_ChargerPrintTxFrame(data);
#if (CHARGER_CAN_TX_DEBUG_PRINT != 0U)
	BMS_INFO("CAN TX mb=%u state=%u err=%u tec=%u rec=%u bo=%u pe=%u we=%u",
	         (unsigned int)tx_status.Mailbox,
	         (unsigned int)tx_status.State,
	         (unsigned int)tx_status.Error,
	         (unsigned int)tx_status.TxErrorCount,
	         (unsigned int)tx_status.RxErrorCount,
	         (unsigned int)tx_status.BusOff,
	         (unsigned int)tx_status.PassiveError,
	         (unsigned int)tx_status.WarningError);
#endif

	if (BMS_ChargerData.FaultClearPending == true) {
		BMS_ChargerData.FaultClearPending = false;
	}
}

/**
 * @brief 打印充电器控制CAN帧用于调试。
 * @param data 待打印的 8 字节充电器控制帧数据区。
 * @return 无返回值。
 */
static void BMS_ChargerPrintTxFrame(const uint8_t *data)
{
#if (CHARGER_CAN_TX_DEBUG_PRINT != 0U)
	BMS_INFO("TX 0x%03X: %02X %02X %02X %02X %02X %02X %02X %02X",
	         (unsigned int)CHARGER_CAN_ID_CONTROL,
	         (unsigned int)data[0],
	         (unsigned int)data[1],
	         (unsigned int)data[2],
	         (unsigned int)data[3],
	         (unsigned int)data[4],
	         (unsigned int)data[5],
	         (unsigned int)data[6],
	         (unsigned int)data[7]);
#else
	(void)data;
#endif
}

/**
 * @brief 判断是否已收到有效的充电器遥测或状态数据。
 * @param 无。
 * @return 已收到有效遥测或状态帧时返回 true，否则返回 false。
 */
static bool BMS_ChargerOnline(void)
{
	return (BMS_ChargerData.Telemetry.Valid == true ||
	        BMS_ChargerData.Status.Valid == true);
}

/**
 * @brief 综合本地许可、保护告警和SOC判断是否允许充电。
 * @param 无。
 * @return 满足全部充电条件时返回 true，否则返回 false。
 */
static bool BMS_ChargerBatteryAllowsCharge(void)
{
	if (BMS_GlobalParam.Charge != BMS_STATE_ENABLE) {
		return false;
	}

	if (BMS_ProtectAlert != FlAG_ALERT_NO && BMS_ProtectAlert != FlAG_ALERT_UV) {
		return false;
	}

	if (BMS_AnalysisIsSocReady() == true &&
	    BMS_AnalysisData.SOC >= 1.0f) {
		return false;
	}

	return true;
}

/**
 * @brief 进入充电器故障停机状态并关闭本地充电。
 * @param 无。
 * @return 无返回值。
 */
static void BMS_ChargerEnterFaultStop(void)
{
	if (BMS_ChargerData.FaultStop == false) {
		BMS_WARNING("Fault status=%u latched=0x%04X present=0x%04X",
		            (unsigned int)BMS_ChargerData.Status.ChargerStatus,
		            (unsigned int)BMS_ChargerData.Status.LatchedFault,
		            (unsigned int)BMS_ChargerData.Status.PresentFault);
	}

	BMS_ChargerData.FaultStop = true;
	BMS_ChargerData.State = BMS_CHARGER_STATE_FAULT;
	BMS_ChargerStopLocalCharge();
}

/**
 * @brief 备份充电许可并关闭本地CHG通路。
 * @param 无。
 * @return 无返回值。
 */
static void BMS_ChargerStopLocalCharge(void)
{
	if (BMS_ChargerChargeBackupValid == false) {
		BMS_ChargerChargeBackup = BMS_GlobalParam.Charge;
		BMS_ChargerChargeBackupValid = true;
	}

	BMS_GlobalParam.Charge = BMS_STATE_DISABLE;
	BMS_HalCtrlCharge(BMS_STATE_DISABLE);
}

/**
 * @brief 在故障解除后恢复先前备份的充电许可。
 * @param 无。
 * @return 无返回值。
 */
static void BMS_ChargerRestoreLocalCharge(void)
{
	if (BMS_ChargerChargeBackupValid == true) {
		BMS_GlobalParam.Charge = BMS_ChargerChargeBackup;
		BMS_ChargerChargeBackupValid = false;
	}
}

/**
 * @brief 按小端格式将16位数据写入字节缓冲区。
 * @param data 目标字节缓冲区。
 * @param offset 缓冲区字节偏移。
 * @param value 要写入的 16 位数值。
 * @return 无返回值。
 */
static void BMS_ChargerPutU16(uint8_t *data, uint8_t offset, uint16_t value)
{
	data[offset] = (uint8_t)(value & 0xFFU);
	data[offset + 1U] = (uint8_t)((value >> 8U) & 0xFFU);
}

/**
 * @brief 按小端格式从字节缓冲区读取16位数据。
 * @param data 源字节缓冲区。
 * @param offset 缓冲区字节偏移。
 * @return 返回从指定偏移处按小端格式解析出的 16 位数值。
 */
static uint16_t BMS_ChargerGetU16(const uint8_t *data, uint8_t offset)
{
	return (uint16_t)data[offset] | ((uint16_t)data[offset + 1U] << 8U);
}

/**
 * @brief 按指定单位缩放32位数值并饱和转换为16位值。
 * @param value 待缩放的 32 位数值。
 * @param unit 缩放单位，即计算时使用的除数。
 * @return 返回 value/unit 并饱和限制到 0～65535；unit 为 0 时返回 0。
 */
static uint16_t BMS_ChargerScaleToU16(uint32_t value, uint32_t unit)
{
	uint32_t scaled;

	if (unit == 0U) {
		return 0U;
	}

	scaled = value / unit;
	if (scaled > 65535U) {
		return 65535U;
	}

	return (uint16_t)scaled;
}
