#define BMS_DBG_TAG "Energy"

#include <stdio.h>
#include <stdbool.h>


#include "bms_energy.h"

#include "bms_hal_control.h"
#include "bms_hal_monitor.h"

#include "bms_monitor.h"
#include "bms_analysis.h"
#include "bms_Protect.h"
#include "bms_global.h"
#include "bms_debug.h"
#include "main.h"



// thread config
#define ENERGY_TASK_STACK_SIZE	1024
#define ENERGY_TASK_PRIORITY	12
#define ENERGY_TASK_TIMESLICE	25

#define ENERGY_TASK_PERIOD		200




BMS_EnergyDataTypedef BMS_EnergyData = 
{
	.SocStopChg   	= SOC_STOP_CHG_VALUE,
	.SocStartChg  	= SOC_START_CHG_VALUE,
	.SocStopDsg   	= SOC_STOP_DSG_VALUE,
	.SocStartDsg  	= SOC_START_DSG_VALUE,
	.PrechargeStartVoltage = PRECHARGE_START_VOLTAGE,
	.PrechargeStopVoltage = PRECHARGE_STOP_VOLTAGE,
	.PredischargeTimeMs = PREDISCHARGE_TIME_MS,
	.OutputMode = BMS_ENERGY_OUTPUT_OFF,

	.BalanceStartVoltage = INIT_BALANCE_VOLTAGE,
	.BalanceDiffeVoltage = BALANCE_DIFFE_VOLTAGE,
	.BalanceCycleTime 	 = BALANCE_CYCLE_TIME,
	.BalanceRecord 		 = BMS_CELL_NULL,
};


// 使用信号量做一下均衡释放
osSemaphoreId_t BalanceSem = NULL;


static osTimerId_t pTimerBalance;
static bool BalanceFlag = false;
static bool BalanceSemHeld = false;
static uint32_t BalanceVoltRiseTime;
static uint32_t BMS_PredischargeStartTick;

static BMS_StateTypedef BMS_CHGStateBackup;
static BMS_StateTypedef BMS_DSGStateBackup;
static bool BMS_CHGOutputValid = false;
static bool BMS_DSGOutputValid = false;
static bool BMS_PCHGOutputValid = false;
static bool BMS_PDSGOutputValid = false;
static BMS_StateTypedef BMS_CHGOutputState = BMS_STATE_DISABLE;
static BMS_StateTypedef BMS_DSGOutputState = BMS_STATE_DISABLE;
static BMS_StateTypedef BMS_PCHGOutputState = BMS_STATE_DISABLE;
static BMS_StateTypedef BMS_PDSGOutputState = BMS_STATE_DISABLE;

typedef enum
{
	BMS_KEY_MODE_CHARGE = BMS_KEY_SWITCH_MODE_CHARGE,
	BMS_KEY_MODE_OFF = BMS_KEY_SWITCH_MODE_OFF,
	BMS_KEY_MODE_DISCHARGE = BMS_KEY_SWITCH_MODE_DISCHARGE,
} BMS_KeyModeTypedef;

static BMS_KeyModeTypedef BMS_KeyMode = BMS_KEY_MODE_CHARGE;
static bool KeyLevelValid = false;
static bool LastKeyPressed = false;

const osThreadAttr_t energyTask_attributes = {
  .name = "energyTask",
  .stack_size = ENERGY_TASK_STACK_SIZE,
  .priority = (osPriority_t) osPriorityNormal2,
};

const osTimerAttr_t energyTimer_attributes = {
  .name = "energyTimer",
};

const osSemaphoreAttr_t energySemaphore_attributes = {
  .name = "energySemaphore",
};


static void BMS_EnergyTaskEntry(void *paramter);

/**
 * @brief 根据全局充放电许可推导当前按键输出模式。
 * @param 无。
 * @return 返回充电、关闭或放电三种按键模式之一。
 */
static BMS_KeyModeTypedef BMS_EnergyGetKeyModeFromGlobal(void)
{
	if (BMS_GlobalParam.Charge == BMS_STATE_ENABLE &&
		BMS_GlobalParam.Discharge != BMS_STATE_ENABLE)
	{
		return BMS_KEY_MODE_CHARGE;
	}

	if (BMS_GlobalParam.Discharge == BMS_STATE_ENABLE &&
		BMS_GlobalParam.Charge != BMS_STATE_ENABLE)
	{
		return BMS_KEY_MODE_DISCHARGE;
	}

	return BMS_KEY_MODE_OFF;
}

/**
 * @brief 把按键模式转换为全局充放电许可。
 * @param mode 要应用的充电、关闭或放电按键模式。
 * @return 无返回值。
 */
static void BMS_EnergyApplyKeyMode(BMS_KeyModeTypedef mode)
{
	switch (mode)
	{
		case BMS_KEY_MODE_OFF:
			BMS_KeyMode = BMS_KEY_MODE_OFF;
			BMS_GlobalParam.Charge = BMS_STATE_DISABLE;
			BMS_GlobalParam.Discharge = BMS_STATE_DISABLE;
			BMS_INFO("Key Mode Off");
		break;

		case BMS_KEY_MODE_DISCHARGE:
			BMS_KeyMode = BMS_KEY_MODE_DISCHARGE;
			BMS_GlobalParam.Charge = BMS_STATE_DISABLE;
			BMS_GlobalParam.Discharge = BMS_STATE_ENABLE;
			BMS_INFO("Key Mode Discharge");
		break;

		case BMS_KEY_MODE_CHARGE:
		default:
			BMS_KeyMode = BMS_KEY_MODE_CHARGE;
			BMS_GlobalParam.Charge = BMS_STATE_ENABLE;
			BMS_GlobalParam.Discharge = BMS_STATE_DISABLE;
			BMS_INFO("Key Mode Charge");
		break;
	}
}

/**
 * @brief 扫描模式按键边沿并循环切换充电、关闭和放电模式。
 * @param 无。
 * @return 无返回值。
 */
static void BMS_EnergyModeKeyScan(void)
{
#if (BMS_OUTPUT_MODE != BMS_OUTPUT_MODE_KEY_SWITCH)
	return;
#else
	bool key_pressed = (gpio_input_bit_get(KEY_MODE_GPIO_Port, KEY_MODE_Pin) == RESET);

	if (KeyLevelValid == false)
	{
		KeyLevelValid = true;
		LastKeyPressed = key_pressed;
		return;
	}

	if (LastKeyPressed != key_pressed)
	{
		LastKeyPressed = key_pressed;

		switch (BMS_KeyMode)
		{
			case BMS_KEY_MODE_CHARGE:
				BMS_EnergyApplyKeyMode(BMS_KEY_MODE_OFF);
			break;

			case BMS_KEY_MODE_OFF:
				BMS_EnergyApplyKeyMode(BMS_KEY_MODE_DISCHARGE);
			break;

			case BMS_KEY_MODE_DISCHARGE:
			default:
				BMS_EnergyApplyKeyMode(BMS_KEY_MODE_CHARGE);
			break;
		}
	}
#endif
}
/**
 * @brief 根据充电许可和SOC阈值判断是否允许充电。
 * @param 无。
 * @return 充电许可有效且 SOC 未达到停止阈值时返回 true，否则返回 false。
 */
static bool BMS_EnergyChargeAllowed(void)
{
	return (BMS_GlobalParam.Charge == BMS_STATE_ENABLE &&
			BMS_AnalysisData.SOC < BMS_EnergyData.SocStopChg);
}
/**
 * @brief 根据放电许可和SOC阈值判断是否允许放电。
 * @param 无。
 * @return 放电许可有效且 SOC 高于停止阈值时返回 true，否则返回 false。
 */
static bool BMS_EnergyDischargeAllowed(void)
{
	return (BMS_GlobalParam.Discharge == BMS_STATE_ENABLE &&
			BMS_AnalysisData.SOC > BMS_EnergyData.SocStopDsg);
}

/**
 * @brief 控制充电输出并避免重复写入相同状态。
 * @param NewState 目标使能状态。
 * @return 无返回值。
 */
static void BMS_EnergySetCharge(BMS_StateTypedef NewState)
{
	if (BMS_CHGOutputValid == true && BMS_CHGOutputState == NewState)
	{
		return;
	}

	BMS_HalCtrlCharge(NewState);
	BMS_CHGOutputState = NewState;
	BMS_CHGOutputValid = true;
	BMS_INFO("%s Charge", (NewState == BMS_STATE_ENABLE) ? "Start" : "Stop");
}

/**
 * @brief 控制放电输出并避免重复写入相同状态。
 * @param NewState 目标使能状态。
 * @return 无返回值。
 */
static void BMS_EnergySetDischarge(BMS_StateTypedef NewState)
{
	if (BMS_DSGOutputValid == true && BMS_DSGOutputState == NewState)
	{
		return;
	}

	BMS_HalCtrlDischarge(NewState);
	BMS_DSGOutputState = NewState;
	BMS_DSGOutputValid = true;
	BMS_INFO("%s Discharge", (NewState == BMS_STATE_ENABLE) ? "Start" : "Stop");
}
/**
 * @brief 控制预充输出并避免重复写入相同状态。
 * @param NewState 目标使能状态。
 * @return 无返回值。
 */
static void BMS_EnergySetPrecharge(BMS_StateTypedef NewState)
{
	if (BMS_PCHGOutputValid == true && BMS_PCHGOutputState == NewState)
	{
		return;
	}

	BMS_HalCtrlPrecharge(NewState);
	BMS_PCHGOutputState = NewState;
	BMS_PCHGOutputValid = true;
	BMS_INFO("%s Precharge", (NewState == BMS_STATE_ENABLE) ? "Start" : "Stop");
}

/**
 * @brief 控制预放输出并避免重复写入相同状态。
 * @param NewState 目标使能状态。
 * @return 无返回值。
 */
static void BMS_EnergySetPredischarge(BMS_StateTypedef NewState)
{
	if (BMS_PDSGOutputValid == true && BMS_PDSGOutputState == NewState)
	{
		return;
	}

	BMS_HalCtrlPredischarge(NewState);
	BMS_PDSGOutputState = NewState;
	BMS_PDSGOutputValid = true;
	BMS_INFO("%s Predischarge", (NewState == BMS_STATE_ENABLE) ? "Start" : "Stop");
}

/**
 * @brief 返回能量输出模式对应的调试名称。
 * @param mode 要转换为字符串的能量输出模式。
 * @return 返回对应的字符串指针。
 */
static const char *BMS_EnergyOutputName(BMS_EnergyOutputTypedef mode)
{
	switch (mode)
	{
		case BMS_ENERGY_OUTPUT_PRECHARGE: return "Precharge";
		case BMS_ENERGY_OUTPUT_CHARGE: return "Charge";
		case BMS_ENERGY_OUTPUT_PREDISCHARGE: return "Predischarge";
		case BMS_ENERGY_OUTPUT_DISCHARGE: return "Discharge";
		default: return "Off";
	}
}

/**
 * @brief 按照目标模式统一切换CHG、DSG、预充和预放输出。
 * @param mode 要切换到的能量输出模式。
 * @return 无返回值。
 */
static void BMS_EnergySetOutput(BMS_EnergyOutputTypedef mode)
{
	switch (mode)
	{
		case BMS_ENERGY_OUTPUT_PRECHARGE:
			BMS_EnergySetCharge(BMS_STATE_DISABLE);
			BMS_EnergySetPredischarge(BMS_STATE_DISABLE);
			BMS_EnergySetDischarge(BMS_STATE_ENABLE);
			BMS_EnergySetPrecharge(BMS_STATE_ENABLE);
		break;

		case BMS_ENERGY_OUTPUT_CHARGE:
			BMS_EnergySetPrecharge(BMS_STATE_DISABLE);
			BMS_EnergySetPredischarge(BMS_STATE_DISABLE);
			BMS_EnergySetDischarge(BMS_STATE_DISABLE);
			BMS_EnergySetCharge(BMS_STATE_ENABLE);
		break;

		case BMS_ENERGY_OUTPUT_PREDISCHARGE:
			BMS_EnergySetDischarge(BMS_STATE_DISABLE);
			BMS_EnergySetPrecharge(BMS_STATE_DISABLE);
			BMS_EnergySetCharge(BMS_STATE_ENABLE);
			BMS_EnergySetPredischarge(BMS_STATE_ENABLE);
		break;

		case BMS_ENERGY_OUTPUT_DISCHARGE:
			BMS_EnergySetPredischarge(BMS_STATE_DISABLE);
			BMS_EnergySetPrecharge(BMS_STATE_DISABLE);
			BMS_EnergySetCharge(BMS_STATE_DISABLE);
			BMS_EnergySetDischarge(BMS_STATE_ENABLE);
		break;

		default:
			BMS_EnergySetPrecharge(BMS_STATE_DISABLE);
			BMS_EnergySetPredischarge(BMS_STATE_DISABLE);
			BMS_EnergySetCharge(BMS_STATE_DISABLE);
			BMS_EnergySetDischarge(BMS_STATE_DISABLE);
			mode = BMS_ENERGY_OUTPUT_OFF;
		break;
	}

	if (BMS_EnergyData.OutputMode != mode)
	{
		BMS_EnergyData.OutputMode = mode;
		BMS_INFO("Output %s", BMS_EnergyOutputName(mode));
	}
}

/**
 * @brief 根据配置和单体电压判断是否需要预充。
 * @param 无。
 * @return true表示操作成功，false表示操作失败。
 */
static bool BMS_EnergyShouldPrecharge(void)
{
	float min_voltage;

	if (BMS_MonitorData.CellVoltageEffectiveNumber == 0U ||
		BMS_EnergyData.PrechargeStartVoltage <= 0.0f ||
		BMS_EnergyData.PrechargeStopVoltage <= 0.0f)
	{
		return false;
	}

	min_voltage = BMS_MonitorData.CellData[0].CellVoltage;
	if (BMS_EnergyData.OutputMode == BMS_ENERGY_OUTPUT_PRECHARGE)
	{
		return (min_voltage < BMS_EnergyData.PrechargeStopVoltage);
	}

	return (min_voltage < BMS_EnergyData.PrechargeStartVoltage);
}

/**
 * @brief 根据配置和计时状态判断是否需要预放。
 * @param 无。
 * @return true表示操作成功，false表示操作失败。
 */
static bool BMS_EnergyShouldPredischarge(void)
{
	uint32_t now = osKernelGetTickCount();

	if (BMS_EnergyData.PredischargeTimeMs == 0U)
	{
		return false;
	}

	if (BMS_EnergyData.OutputMode != BMS_ENERGY_OUTPUT_PREDISCHARGE)
	{
		BMS_PredischargeStartTick = now;
		return true;
	}

	return ((uint32_t)(now - BMS_PredischargeStartTick) < BMS_EnergyData.PredischargeTimeMs);
}
/**
 * @brief 关闭均衡通道、释放资源并设置电压恢复等待时间。
 * @param stop_timer 是否同时停止均衡定时器。
 * @param reason 停止均衡的原因说明。
 * @return 无返回值。
 */
static void BMS_EnergyStopBalance(bool stop_timer, const char *reason)
{
	bool was_active = (BalanceFlag == true || BMS_EnergyData.BalanceRecord != BMS_CELL_NULL);

	if (was_active == true)
	{
		if (stop_timer == true && pTimerBalance != NULL)
		{
			(void)osTimerStop(pTimerBalance);
		}

		BMS_HalCtrlCellsBalance(BMS_CELL_ALL, BMS_STATE_DISABLE);
		BMS_EnergyData.BalanceRecord = BMS_CELL_NULL;

		if (BalanceSemHeld == true && BalanceSem != NULL)
		{
			(void)osSemaphoreRelease(BalanceSem);
			BalanceSemHeld = false;
		}

		if (reason != NULL)
		{
			BMS_INFO("External Balance Stop: %s", reason);
		}
	}

	BalanceFlag = false;
	BalanceVoltRiseTime = BALANCE_VOLT_RISE_DELAY + osKernelGetTickCount();
}

/**
 * @brief 处理均衡周期到期事件并停止当前均衡。
 * @param paramter 任务入口参数，本函数未使用。
 * @return 无返回值。
 */
static void BMS_BalanceTimerEntry(void *paramter)
{
	(void)paramter;

	BMS_EnergyStopBalance(false, "timer");
}

/**
 * @brief 启动指定秒数的均衡一次性定时器。
 * @param sec 定时时长，单位为秒。
 * @return 无返回值。
 */
static void BMS_BalanceStartTimer(uint32_t sec)
{
	uint32_t tick;

	if (pTimerBalance == NULL)
	{
		return;
	}

	tick = sec * 1000;
	osTimerStart(pTimerBalance, tick);

	BMS_INFO("Balance Timer Start");
}







/*
 * 函数作用：根据系统模式、SOC 和用户开关状态自动控制充电/放电 MOS。
 */
/**
 * @brief 根据系统模式、SOC 和用户开关状态自动控制充电/放电 MOS。
 * @param 无。
 * @return 无返回值。
 */
static void BMS_EnergyChgDsgManage(void)
{
	BMS_EnergyOutputTypedef target_mode = BMS_ENERGY_OUTPUT_OFF;
	bool uv_recovery = (BMS_ProtectAlert == FlAG_ALERT_UV);

	if (BMS_ProtectAlert != FlAG_ALERT_NO && BMS_ProtectAlert != FlAG_ALERT_UV)
	{
		BMS_EnergySetOutput(BMS_ENERGY_OUTPUT_OFF);
		return;
	}

	if (BMS_AnalysisIsSocReady() == false)
	{
		BMS_EnergySetOutput(BMS_ENERGY_OUTPUT_OFF);
		return;
	}

	if (BMS_EnergyChargeAllowed() == true)
	{
		if (BMS_EnergyShouldPrecharge() == true)
		{
			target_mode = BMS_ENERGY_OUTPUT_PRECHARGE;
		}
		else
		{
			target_mode = BMS_ENERGY_OUTPUT_CHARGE;
		}
	}
	else if (uv_recovery == false && BMS_EnergyDischargeAllowed() == true)
	{
		if (BMS_EnergyShouldPredischarge() == true)
		{
			target_mode = BMS_ENERGY_OUTPUT_PREDISCHARGE;
		}
		else
		{
			target_mode = BMS_ENERGY_OUTPUT_DISCHARGE;
		}
	}

	if (target_mode == BMS_ENERGY_OUTPUT_CHARGE)
	{
		if (BalanceSem == NULL)
		{
			BMS_EnergySetOutput(target_mode);
		}
		else if (osSemaphoreAcquire(BalanceSem, 0) == osOK)
		{
			BMS_EnergySetOutput(target_mode);
			osSemaphoreRelease(BalanceSem);
		}
	}
	else
	{
		BMS_EnergySetOutput(target_mode);
	}

	BMS_CHGStateBackup = BMS_GlobalParam.Charge;
	BMS_DSGStateBackup = BMS_GlobalParam.Discharge;
}
/**
 * @brief 根据单体压差选择非相邻高压单体并管理周期均衡。
 * @param 无。
 * @return 无返回值。
 */
static void BMS_EnergyBalanceManage(void)
{
	uint8_t index;
	uint8_t configured_count = BMS_GlobalParam.Cell_Real_Number;
	uint32_t effective_count = BMS_MonitorData.CellVoltageEffectiveNumber;
	uint8_t cell_count;
	uint8_t selected_count = 0U;
	uint8_t max_balance_cells = EXTERNAL_BALANCE_MAX_CELLS;
	uint16_t balance_record = BMS_CELL_NULL;
	float MinVoltage;
	float MaxVoltage;
	uint32_t now = osKernelGetTickCount();

#if (EXTERNAL_BALANCE_ENABLE == 0U)
	BMS_EnergyStopBalance(true, "cfg off");
	return;
#endif

	if (configured_count == 0U || configured_count > BMS_CELL_MAX)
	{
		configured_count = BMS_CELL_MAX;
	}

	if (max_balance_cells > configured_count)
	{
		max_balance_cells = configured_count;
	}

	if (BMS_GlobalParam.Balance == BMS_STATE_DISABLE || max_balance_cells == 0U)
	{
		BMS_EnergyStopBalance(true, "disabled");
		return;
	}

	if (BMS_ProtectAlert != FlAG_ALERT_NO)
	{
		BMS_INFO("External Balance Wait: protect=0x%04X", (unsigned int)BMS_ProtectAlert);
		BMS_EnergyStopBalance(true, "protect");
		return;
	}

	if ((BMS_GlobalParam.SysMode != BMS_MODE_STANDBY) &&
		(BMS_GlobalParam.SysMode != BMS_MODE_CHARGE))
	{
		BMS_INFO("External Balance Wait: mode=%d", BMS_GlobalParam.SysMode);
		BMS_EnergyStopBalance(true, "mode");
		return;
	}

	if (BalanceFlag == true)
	{
		return;
	}

	if ((int32_t)(now - BalanceVoltRiseTime) < 0)
	{
		return;
	}

	if (effective_count == 0U)
	{
		BMS_INFO("External Balance Wait: no valid cell voltage");
		BalanceVoltRiseTime = BALANCE_VOLT_RISE_DELAY + now;
		return;
	}

	if (effective_count > configured_count)
	{
		effective_count = configured_count;
	}
	cell_count = (uint8_t)effective_count;

	MinVoltage = BMS_MonitorData.CellData[0].CellVoltage;
	MaxVoltage = BMS_MonitorData.CellData[cell_count - 1U].CellVoltage;
	if (MaxVoltage <= BMS_EnergyData.BalanceStartVoltage)
	{
		BMS_INFO("External Balance Wait: vmax=%.3fV start=%.3fV",
			MaxVoltage,
			BMS_EnergyData.BalanceStartVoltage);
		BMS_EnergyData.BalanceRecord = BMS_CELL_NULL;
		BalanceVoltRiseTime = BALANCE_VOLT_RISE_DELAY + now;
		return;
	}

	if (BalanceSem == NULL || osSemaphoreAcquire(BalanceSem, 0) != osOK)
	{
		return;
	}
	BalanceSemHeld = true;

	for(index = 1U; index <= cell_count; index++)
	{
		float CmpVoltage = BMS_MonitorData.CellData[cell_count - index].CellVoltage;
		uint8_t CellNumber = (uint8_t)BMS_MonitorData.CellData[cell_count - index].CellNumber;
		bool result = true;

		if (selected_count >= max_balance_cells)
		{
			break;
		}

		if (CmpVoltage - MinVoltage <= BMS_EnergyData.BalanceDiffeVoltage)
		{
			break;
		}

		if (CellNumber >= configured_count)
		{
			continue;
		}

		if (CellNumber > 0U && (balance_record & (1U << (CellNumber - 1U))) != 0U)
		{
			result = false;
		}
		if ((CellNumber + 1U) < configured_count && (balance_record & (1U << (CellNumber + 1U))) != 0U)
		{
			result = false;
		}
		
		if (result == true)
		{
			BMS_INFO("External Balance Cell:%d v=%.3fV delta=%.3fV", CellNumber + 1U, CmpVoltage, CmpVoltage - MinVoltage);
			balance_record |= 1U << CellNumber;
			selected_count++;
		}
	}

	BMS_EnergyData.BalanceRecord = (BMS_CellIndexTypedef)balance_record;
	if (BMS_EnergyData.BalanceRecord != BMS_CELL_NULL)
	{
		BMS_HalCtrlCellsBalance(BMS_EnergyData.BalanceRecord, BMS_STATE_ENABLE);
		BMS_BalanceStartTimer(BMS_EnergyData.BalanceCycleTime);

		BalanceFlag = true;
		
		BMS_INFO("External Balance Start mask=0x%04X cells=%u", (unsigned int)BMS_EnergyData.BalanceRecord, selected_count);

		(void)osSemaphoreRelease(BalanceSem);
		BalanceSemHeld = false;
		return;
	}

	BMS_INFO("External Balance Wait: delta=%.3fV need=%.3fV",
		MaxVoltage - MinVoltage,
		BMS_EnergyData.BalanceDiffeVoltage);
	(void)osSemaphoreRelease(BalanceSem);
	BalanceSemHeld = false;
	BalanceVoltRiseTime = BALANCE_VOLT_RISE_DELAY + now;
}
/**
 * @brief 运行能量管理任务，周期处理按键、输出和均衡。
 * @param paramter 任务入口参数，本函数未使用。
 * @return 无返回值。
 */
static void BMS_EnergyTaskEntry(void *paramter)
{
	(void)paramter;

	while (1) {
		BMS_EnergyModeKeyScan();
		BMS_EnergyChgDsgManage();
		BMS_EnergyBalanceManage();
		osDelay(ENERGY_TASK_PERIOD);
	}
}

/**
 * @brief 初始化能量状态、信号量、定时器并创建任务。
 * @param 无。
 * @return 无返回值。
 */
void BMS_EnergyInit(void)
{
	osThreadId_t thread;

	BalanceFlag = false;
	BMS_EnergyData.BalanceRecord = BMS_CELL_NULL;
	BalanceVoltRiseTime = BALANCE_VOLT_RISE_DELAY + osKernelGetTickCount();
	BMS_CHGStateBackup = BMS_GlobalParam.Charge;
	BMS_DSGStateBackup = BMS_GlobalParam.Discharge;
	BMS_KeyMode = BMS_EnergyGetKeyModeFromGlobal();
	KeyLevelValid = false;
	LastKeyPressed = false;
	BMS_CHGOutputValid = false;
	BMS_DSGOutputValid = false;
	BMS_PCHGOutputValid = false;
	BMS_PDSGOutputValid = false;
	BMS_CHGOutputState = BMS_STATE_DISABLE;
	BMS_DSGOutputState = BMS_STATE_DISABLE;
	BMS_PCHGOutputState = BMS_STATE_DISABLE;
	BMS_PDSGOutputState = BMS_STATE_DISABLE;
	BMS_PredischargeStartTick = 0U;
	BMS_EnergyData.OutputMode = BMS_ENERGY_OUTPUT_OFF;

	BalanceSem = osSemaphoreNew(1U, 1U, &energySemaphore_attributes);
	if (BalanceSem == NULL) {
		BMS_ERROR("Create Semaphore Fail");
	}

	pTimerBalance = osTimerNew(BMS_BalanceTimerEntry,
							   osTimerOnce,
							   NULL,
							   &energyTimer_attributes);
	if (pTimerBalance == NULL) {
		BMS_ERROR("Create Timer Fail");
	}

	thread = osThreadNew(BMS_EnergyTaskEntry, NULL, &energyTask_attributes);
	if (thread == NULL) {
		BMS_ERROR("Create Task Fail");
	}
}
