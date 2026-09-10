#define BMS_DBG_TAG "Protect"

#include <stdio.h>
#include <stdbool.h>
#include "cmsis_os2.h"

#include "bms_protect.h"

#include "bms_hal_monitor.h"
#include "bms_hal_control.h"
#include "bms_monitor.h"
#include "bms_global.h"
#include "bms_debug.h"




// thread config
#define PROTECT_TASK_STACK_SIZE	1024
#define PROTECT_TASK_PRIORITY	10
#define PROTECT_TASK_TIMESLICE	25

#define PROTECT_TASK_PERIOD		200



volatile BMS_ProtectAlertTypedef BMS_ProtectAlert = FlAG_ALERT_NO;
BMS_ProtectParamTypedef BMS_ProtectParam = 
{
	.ShoutdownVoltage = INIT_SHUTDOWN_VOLTAGE,

	.OVProtect	= INIT_OV_PROTECT,
	.OVRelieve	= INIT_OV_RELIEVE,
	.UVProtect	= INIT_UV_PROTECT,
	.UVRelieve	= INIT_UV_RELIEVE,

	.OCCProtect = INIT_OCC_MAX,
	.OCDProtect = INIT_OCD_MAX,

	.OVDelay	= INIT_OV_DELAY,
	.UVDelay	= INIT_UV_DELAY,
	.OCDDelay	= INIT_OCD_DELAY,
	.SCDDelay	= INIT_SCD_DELAY,

	.OCDRelieve = INIT_OCD_RELIEVE,
	.SCDRelieve = INIT_SCD_RELIEVE,
	.OCCDelay	= INIT_OCC_DELAY,
	.OCCRelieve = INIT_OCC_RELIEVE,

	.OTCProtect = INIT_OTC_PROTECT,
	.OTCRelieve = INIT_OTC_RELIEVE,
	.OTDProtect = INIT_OTD_PROTECT,
	.OTDRelieve = INIT_OTD_RELIEVE,

	.LTCProtect = INIT_LTC_PROTECT,
	.LTCRelieve = INIT_LTC_RELIEVE,
	.LTDProtect = INIT_LTD_PROTECT,
	.LTDRelieve = INIT_LTD_RELIEVE,
};

static osTimerId_t pTimerProtect;
static volatile BMS_ProtectStateTypedef ProtectState = PROTECT_STATE_MONITOR;
static uint32_t OccStartTick = 0U;
static uint32_t OcdStartTick = 0U;
static uint32_t OvStartTick = 0U;
static uint32_t UvStartTick = 0U;
static bool OccDelayActive = false;
static bool OcdDelayActive = false;
static bool OvDelayActive = false;
static bool UvDelayActive = false;
const osThreadAttr_t protectTask_attributes = {
  .name = "protectTask",
  .stack_size = PROTECT_TASK_STACK_SIZE,
  .priority = (osPriority_t) osPriorityNormal4,
};

const osTimerAttr_t protectTimer_attributes = {
  .name = "protectTimer",
};


static void BMS_ProtectTimerEntry(void *paramter);
static void BMS_ProtectTaskEntry(void *paramter);
static void BMS_ProtectStartTimer(uint32_t sec);
static bool BMS_ProtectHasValidTemp(void);
static bool BMS_ProtectHasValidCells(void);
static float BMS_ProtectMinCellVoltage(void);
static float BMS_ProtectMaxCellVoltage(void);
static float BMS_ProtectMinTemp(void);
static float BMS_ProtectMaxTemp(void);
static uint32_t BMS_ProtectOvDelayMs(BMS_OVDelayTypedef delay);
static uint32_t BMS_ProtectUvDelayMs(BMS_UVDelayTypedef delay);
static uint32_t BMS_ProtectOcdDelayMs(BMS_OCDDelayTypedef delay);
static void BMS_ProtectSwMonitor(void);
/**
 * @brief 检查当前保护类型是否已经满足恢复条件。
 * @param 无。
 * @return 无返回值。
 */
static void BMS_ProtectRelieveWait(void)
{
	switch (BMS_ProtectAlert)
	{
		case FlAG_ALERT_OV:
		{
			if (BMS_ProtectHasValidCells() == true && BMS_ProtectMaxCellVoltage() < BMS_ProtectParam.OVRelieve)
			{
				ProtectState = PROTECT_STATE_RELIEVE;

				BMS_INFO("OV Relieve");
			}
		}break;

		case FlAG_ALERT_UV:
		{
			if (BMS_ProtectHasValidCells() == true && BMS_ProtectMinCellVoltage() > BMS_ProtectParam.UVRelieve)
			{
				ProtectState = PROTECT_STATE_RELIEVE;

				BMS_INFO("UV Relieve");
			}
		}break;

		case FlAG_ALERT_OTC:
		{
			if (BMS_ProtectHasValidTemp() == true && BMS_ProtectMaxTemp() < BMS_ProtectParam.OTCRelieve)
			{
				ProtectState = PROTECT_STATE_RELIEVE;

				BMS_INFO("OTC Relieve");
			}
		}break;

		case FlAG_ALERT_OTD:
		{
			if (BMS_ProtectHasValidTemp() == true && BMS_ProtectMaxTemp() < BMS_ProtectParam.OTDRelieve)
			{
				ProtectState = PROTECT_STATE_RELIEVE;

				BMS_INFO("OTD Relieve");
			}
		}break;

		case FlAG_ALERT_LTC:
		{
			if (BMS_ProtectHasValidTemp() == true && BMS_ProtectMinTemp() > BMS_ProtectParam.LTCRelieve)
			{
				ProtectState = PROTECT_STATE_RELIEVE;

				BMS_INFO("LTC Relieve");
			}
		}break;

		case FlAG_ALERT_LTD:
		{
			if (BMS_ProtectHasValidTemp() == true && BMS_ProtectMinTemp() > BMS_ProtectParam.LTDRelieve)
			{
				ProtectState = PROTECT_STATE_RELIEVE;

				BMS_INFO("LTD Relieve");
			}
		}break;

		default:
		{

		}
		break;
	}
}


/**
 * @brief 恢复受保护输出并清除告警状态。
 * @param 无。
 * @return 无返回值。
 */
static void BMS_ProtectRelieve(void)
{
	switch(BMS_ProtectAlert)
	{
		case FlAG_ALERT_OCC:
		case FlAG_ALERT_OTC:
		case FlAG_ALERT_LTC:
			BMS_HalCtrlCharge(BMS_GlobalParam.Charge);
		break;

		case FlAG_ALERT_OCD:
		case FlAG_ALERT_OTD:
		case FlAG_ALERT_LTD:
		case FlAG_ALERT_SCD:
			BMS_HalCtrlDischarge(BMS_GlobalParam.Discharge);
		break;

		case FlAG_ALERT_OV:
			BMS_HalCtrlCharge(BMS_GlobalParam.Charge);
		break;

		case FlAG_ALERT_UV:
			BMS_HalCtrlDischarge(BMS_GlobalParam.Discharge);
		break;

		default:;break;
	}
	
	BMS_ProtectAlert = FlAG_ALERT_NO;	
	ProtectState = PROTECT_STATE_MONITOR;

	BMS_INFO("Protect Relieve");
}

/**
 * @brief 判断是否存在有效温度采样。
 * @param 无。
 * @return 存在至少一个有效温度采样时返回 true，否则返回 false。
 */
static bool BMS_ProtectHasValidTemp(void)
{
	return (BMS_MonitorData.CellTempEffectiveNumber > 0U);
}

/**
 * @brief 判断是否存在有效单体电压采样。
 * @param 无。
 * @return 存在至少一节有效单体电压时返回 true，否则返回 false。
 */
static bool BMS_ProtectHasValidCells(void)
{
	return (BMS_MonitorData.CellVoltageEffectiveNumber > 0U);
}
/**
 * @brief 取得有效单体中的最低电压。
 * @param 无。
 * @return 返回最低有效单体电压；无有效单体时返回 0.0 V。
 */
static float BMS_ProtectMinCellVoltage(void)
{
	if (BMS_ProtectHasValidCells() == false)
	{
		return 0.0f;
	}

	return BMS_MonitorData.CellData[0].CellVoltage;
}
/**
 * @brief 取得有效单体中的最高电压。
 * @param 无。
 * @return 返回最高有效单体电压；无有效单体时返回 0.0 V。
 */
static float BMS_ProtectMaxCellVoltage(void)
{
	uint32_t cell_count = BMS_MonitorData.CellVoltageEffectiveNumber;

	if (cell_count == 0U)
	{
		return 0.0f;
	}

	if (cell_count > BMS_CELL_MAX)
	{
		cell_count = BMS_CELL_MAX;
	}

	return BMS_MonitorData.CellData[cell_count - 1U].CellVoltage;
}
/**
 * @brief 取得有效温度中的最低值。
 * @param 无。
 * @return 返回最低有效温度，单位为摄氏度。
 */
static float BMS_ProtectMinTemp(void)
{
	return BMS_MonitorData.CellTemp[0];
}

/**
 * @brief 取得有效温度中的最高值。
 * @param 无。
 * @return 返回最高有效温度；无有效温度时返回 0.0 ℃。
 */
static float BMS_ProtectMaxTemp(void)
{
	uint8_t temp_count = BMS_MonitorData.CellTempEffectiveNumber;

	if (temp_count == 0U) {
		return 0.0f;
	}

	return BMS_MonitorData.CellTemp[temp_count - 1U];
}

/**
 * @brief 将过压延时枚举转换为毫秒。
 * @param delay 保护延时配置。
 * @return 返回对应的过压保护延时时间，单位为毫秒。
 */
static uint32_t BMS_ProtectOvDelayMs(BMS_OVDelayTypedef delay)
{
	static const uint32_t ms[] = {1000U, 2000U, 4000U, 8000U};
	return ms[(uint8_t)delay & 0x03U];
}

/**
 * @brief 将欠压延时枚举转换为毫秒。
 * @param delay 保护延时配置。
 * @return 返回对应的欠压保护延时时间，单位为毫秒。
 */
static uint32_t BMS_ProtectUvDelayMs(BMS_UVDelayTypedef delay)
{
	static const uint32_t ms[] = {1000U, 4000U, 8000U, 16000U};
	return ms[(uint8_t)delay & 0x03U];
}

/**
 * @brief 将放电过流延时枚举转换为毫秒。
 * @param delay 保护延时配置。
 * @return 返回对应的放电过流保护延时时间，单位为毫秒。
 */
static uint32_t BMS_ProtectOcdDelayMs(BMS_OCDDelayTypedef delay)
{
	static const uint32_t ms[] = {10U, 20U, 40U, 80U, 160U, 320U, 640U, 1280U};
	return ms[(uint8_t)delay & 0x07U];
}
/**
 * @brief 启动保护恢复一次性定时器。
 * @param sec 定时时长，单位为秒。
 * @return 无返回值。
 */
static void BMS_ProtectStartTimer(uint32_t sec)
{
	if (pTimerProtect != NULL) {
		(void)osTimerStart(pTimerProtect, sec * 1000U);
	}
}

/**
 * @brief 处理保护定时器到期并推进保护状态机。
 * @param paramter 任务入口参数，本函数未使用。
 * @return 无返回值。
 */
static void BMS_ProtectTimerEntry(void *paramter)
{
	(void)paramter;

	ProtectState = PROTECT_STATE_RELIEVE;
}

/**
 * @brief 使用最新测量数据执行软件保护检测和延时确认。
 * @param 无。
 * @return 无返回值。
 */
static void BMS_ProtectSwMonitor(void)
{
	uint32_t now = osKernelGetTickCount();

	if (BMS_ProtectHasValidCells() == true)
	{
#if (BMS_OV_PROTECT_ENABLE != 0)
		if (BMS_ProtectMaxCellVoltage() >= BMS_ProtectParam.OVProtect)
		{
			if (OvDelayActive == false)
			{
				OvDelayActive = true;
				OvStartTick = now;
			}
			else if ((now - OvStartTick) >= BMS_ProtectOvDelayMs(BMS_ProtectParam.OVDelay))
			{
				BMS_ProtectAlert = FlAG_ALERT_OV;
				BMS_HalCtrlCharge(BMS_STATE_DISABLE);
				ProtectState = PROTECT_STATE_RELIEVE_WAIT;
				OvDelayActive = false;
				BMS_WARNING("OV Trigger");
				return;
			}
		}
		else
		{
			OvDelayActive = false;
		}

#else
		OvDelayActive = false;
#endif
		if (BMS_ProtectMinCellVoltage() <= BMS_ProtectParam.UVProtect)
		{
			if (UvDelayActive == false)
			{
				UvDelayActive = true;
				UvStartTick = now;
			}
			else if ((now - UvStartTick) >= BMS_ProtectUvDelayMs(BMS_ProtectParam.UVDelay))
			{
				BMS_ProtectAlert = FlAG_ALERT_UV;
				BMS_HalCtrlDischarge(BMS_STATE_DISABLE);
				ProtectState = PROTECT_STATE_RELIEVE_WAIT;
				UvDelayActive = false;
				BMS_WARNING("UV Trigger");
				return;
			}
		}
		else
		{
			UvDelayActive = false;
		}
	}
	else
	{
		OvDelayActive = false;
		UvDelayActive = false;
	}

	if (BMS_ProtectHasValidTemp() == true) {
		if (BMS_GlobalParam.Charge == BMS_STATE_ENABLE) {
			if (BMS_ProtectMaxTemp() >= BMS_ProtectParam.OTCProtect) {
				BMS_ProtectAlert = FlAG_ALERT_OTC;
				BMS_HalCtrlCharge(BMS_STATE_DISABLE);
				ProtectState = PROTECT_STATE_RELIEVE_WAIT;
				BMS_WARNING("OTC Trigger");
				return;
			}
			if (BMS_ProtectMinTemp() <= BMS_ProtectParam.LTCProtect) {
				BMS_ProtectAlert = FlAG_ALERT_LTC;
				BMS_HalCtrlCharge(BMS_STATE_DISABLE);
				ProtectState = PROTECT_STATE_RELIEVE_WAIT;
				BMS_WARNING("LTC Trigger");
				return;
			}
		}

		if (BMS_GlobalParam.Discharge == BMS_STATE_ENABLE) {
			if (BMS_ProtectMaxTemp() >= BMS_ProtectParam.OTDProtect) {
				BMS_ProtectAlert = FlAG_ALERT_OTD;
				BMS_HalCtrlDischarge(BMS_STATE_DISABLE);
				ProtectState = PROTECT_STATE_RELIEVE_WAIT;
				BMS_WARNING("OTD Trigger");
				return;
			}
			if (BMS_ProtectMinTemp() <= BMS_ProtectParam.LTDProtect) {
				BMS_ProtectAlert = FlAG_ALERT_LTD;
				BMS_HalCtrlDischarge(BMS_STATE_DISABLE);
				ProtectState = PROTECT_STATE_RELIEVE_WAIT;
				BMS_WARNING("LTD Trigger");
				return;
			}
		}
	}

	if (BMS_MonitorData.BatteryCurrent <= -BMS_ProtectParam.OCDProtect) {
		if (OcdDelayActive == false) {
			OcdDelayActive = true;
			OcdStartTick = now;
		} else if ((now - OcdStartTick) >= BMS_ProtectOcdDelayMs(BMS_ProtectParam.OCDDelay)) {
			BMS_ProtectAlert = FlAG_ALERT_OCD;
			BMS_HalCtrlDischarge(BMS_STATE_DISABLE);
			BMS_ProtectStartTimer(BMS_ProtectParam.OCDRelieve);
			ProtectState = PROTECT_STATE_RELIEVE_WAIT;
			OcdDelayActive = false;
			BMS_WARNING("OCD Trigger");
			return;
		}
	} else {
		OcdDelayActive = false;
	}

	if (BMS_MonitorData.BatteryCurrent >= BMS_ProtectParam.OCCProtect) {
		if (OccDelayActive == false) {
			OccDelayActive = true;
			OccStartTick = now;
		} else if ((now - OccStartTick) >= ((uint32_t)BMS_ProtectParam.OCCDelay * 1000U)) {
			BMS_ProtectAlert = FlAG_ALERT_OCC;
			BMS_HalCtrlCharge(BMS_STATE_DISABLE);
			BMS_ProtectStartTimer(BMS_ProtectParam.OCCRelieve);
			ProtectState = PROTECT_STATE_RELIEVE_WAIT;
			OccDelayActive = false;
			BMS_WARNING("OCC Trigger");
		}
	} else {
		OccDelayActive = false;
	}
}

/**
 * @brief 根据AFE硬件告警执行对应MOS关断和恢复计时。
 * @param 无。
 * @return 无返回值。
 */
void BMS_ProtectHwMonitor(void)
{
	switch(BMS_ProtectAlert)
	{
		case FlAG_ALERT_OCD:
		{			
			BMS_HalCtrlDischarge(BMS_STATE_DISABLE);
			BMS_ProtectStartTimer(BMS_ProtectParam.OCDRelieve);
			ProtectState = PROTECT_STATE_RELIEVE_WAIT;

			BMS_WARNING("OCD Trigger");
		}break;

		case FlAG_ALERT_SCD:
		{			
			BMS_HalCtrlDischarge(BMS_STATE_DISABLE);
			BMS_ProtectStartTimer(BMS_ProtectParam.SCDRelieve);
			ProtectState = PROTECT_STATE_RELIEVE_WAIT;

			BMS_WARNING("SCD Trigger");
		}break;

#if (BMS_OV_PROTECT_ENABLE != 0)
		case FlAG_ALERT_OV:
		{
			BMS_HalCtrlCharge(BMS_STATE_DISABLE);
			ProtectState = PROTECT_STATE_RELIEVE_WAIT;
			
			BMS_WARNING("OV Trigger");
		}break;
#endif

		case FlAG_ALERT_UV:
		{
			BMS_HalCtrlDischarge(BMS_STATE_DISABLE);
			ProtectState = PROTECT_STATE_RELIEVE_WAIT;

			BMS_WARNING("UV Trigger");
		}break;

		default:
		{

		}break;
	}
}


/**
 * @brief 记录AFE放电过流硬件告警。
 * @param 无。
 * @return 无返回值。
 */
void BMS_ProtectHwOCD(void)
{
	if (BMS_ProtectAlert == FlAG_ALERT_NO)
	{
		BMS_ProtectAlert = FlAG_ALERT_OCD;
	}
}

// 放电短路(SCD)硬件触发
/*
 * 函数作用：BQ769x0 放电短路 SCD 硬件报警回调，置位保护报警标志。
 */
/**
 * @brief 记录 AFE 检测到的放电短路 SCD 硬件告警。
 * @param 无。
 * @return 无返回值。
 */
void BMS_ProtectHwSCD(void)
{
	if (BMS_ProtectAlert == FlAG_ALERT_NO)
	{
		BMS_ProtectAlert = FlAG_ALERT_SCD;
	}
}

// 充电过压(OV)硬件触发
/*
 * 函数作用：BQ769x0 单体过压 OV 硬件报警回调，置位保护报警标志。
 */
/**
 * @brief 记录 AFE 检测到的单体过压 OV 硬件告警。
 * @param 无。
 * @return 无返回值。
 */
void BMS_ProtectHwOV(void)
{
#if (BMS_OV_PROTECT_ENABLE != 0)
	if (BMS_ProtectAlert == FlAG_ALERT_NO)
	{
		BMS_ProtectAlert = FlAG_ALERT_OV;
	}
#endif
}

// 放电欠压(UV)硬件触发
/*
 * 函数作用：BQ769x0 单体欠压 UV 硬件报警回调，置位保护报警标志。
 */
/**
 * @brief 记录 AFE 检测到的单体欠压 UV 硬件告警。
 * @param 无。
 * @return 无返回值。
 */
void BMS_ProtectHwUV(void)
{
	if (BMS_ProtectAlert == FlAG_ALERT_NO)
	{
		BMS_ProtectAlert = FlAG_ALERT_UV;
	}
}


/*
 * 函数作用：BQ769x0 设备故障硬件报警回调，目前用于打印设备故障信息。
 */
/**
 * @brief 处理 AFE 设备故障告警并输出诊断日志。
 * @param 无。
 * @return 无返回值。
 */
void BMS_ProtectHwDevice(void)
{
	BMS_INFO("BMS_ProtectHwDevice");
}


/*
 * 函数作用：BQ769x0 OVRD 相关硬件报警回调，目前用于打印报警信息。
 */
/**
 * @brief 处理 AFE 保护覆盖 OVRD 告警并输出诊断日志。
 * @param 无。
 * @return 无返回值。
 */
void BMS_ProtectHwOvrd(void)
{
	BMS_INFO("BMS_ProtectHwOvrd");
}

/**
 * @brief 运行保护任务并驱动监测、等待恢复和解除状态。
 * @param paramter 任务入口参数，本函数未使用。
 * @return 无返回值。
 */
static void BMS_ProtectTaskEntry(void *paramter)
{
	(void)paramter;

	while (1) {
		switch (ProtectState) {
			case PROTECT_STATE_MONITOR:
				if (BMS_ProtectAlert != FlAG_ALERT_NO) {
					BMS_ProtectHwMonitor();
				} else {
					BMS_ProtectSwMonitor();
				}
				break;

			case PROTECT_STATE_RELIEVE_WAIT:
				BMS_ProtectRelieveWait();
				break;

			case PROTECT_STATE_RELIEVE:
				BMS_ProtectRelieve();
				break;

			default:
				ProtectState = PROTECT_STATE_MONITOR;
				break;
		}

		osDelay(PROTECT_TASK_PERIOD);
	}
}

/**
 * @brief 初始化保护状态、恢复定时器并创建保护任务。
 * @param 无。
 * @return 无返回值。
 */
void BMS_ProtectInit(void)
{
	osThreadId_t thread;

	BMS_ProtectAlert = FlAG_ALERT_NO;
	ProtectState = PROTECT_STATE_MONITOR;
	OccDelayActive = false;
	OcdDelayActive = false;
	OvDelayActive = false;
	UvDelayActive = false;

	pTimerProtect = osTimerNew(BMS_ProtectTimerEntry,
							   osTimerOnce,
							   NULL,
							   &protectTimer_attributes);
	if (pTimerProtect == NULL) {
		BMS_ERROR("Create Timer Fail");
	}

	thread = osThreadNew(BMS_ProtectTaskEntry, NULL, &protectTask_attributes);
	if (thread == NULL) {
		BMS_ERROR("Create Task Fail");
	}
}

