#define BMS_DBG_TAG "Monitor"

#include <stdio.h>
//#include <rtthread.h>

#include "bms_monitor.h"

#include "bms_hal_monitor.h"

#include "bms_energy.h"
#include "bms_global.h"
#include "bms_debug.h"





// thread config
#define MONITOR_TASK_STACK_SIZE	1024
#define MONITOR_TASK_PRIORITY	9
#define MONITOR_TASK_TIMESLICE	25

#define MONITOR_TASK_PERIOD		250



// sample time config  MS
#define UPDATE_CELL_VOLTAGE_CYCLE	250
#define UPDAYE_BAT_VOLTAGE_CYCLE	250
#define UPDATE_CELL_TEMP_CYCLE		2000
#define UPDATE_BAT_CURRENT_CYCLE	1000






BMS_MonitorDataTypedef BMS_MonitorData;



static bool FlagSampleIntCur = false;

static bool FlagCellVoltage = true;
static bool FlagBatVoltage = true;
static bool FlagCellTemp = true;
static bool FlagBatCurrent = true;


static uint16_t CountCellVoltage = 0;
static uint16_t CountBatVoltage = 0;
static uint16_t CountCellTemp = 0;
static uint16_t CountBatCurrent = 0;


const osThreadAttr_t monitorTask_attributes = {
  .name = "monitorTask",
  .stack_size = MONITOR_TASK_STACK_SIZE,
  .priority = (osPriority_t) osPriorityNormal5,
};

static void BMS_MonitorTaskEntry(void *paramter);
static void BMS_MonitorBattery(void);
static void BMS_MonitorSysMode(void);


/*
 * 函数作用：创建电池监控任务，启动电压、电流、温度和系统模式的周期监测。
 */
/**
 * @brief 创建电池监控任务，启动电压、电流、温度和系统模式的周期监测。
 * @param 无。
 * @return 无返回值。
 */
void BMS_MonitorInit(void)
{
	osThreadId_t thread;
	
	thread = osThreadNew(BMS_MonitorTaskEntry, NULL, &monitorTask_attributes);

   if (thread == NULL)
   {
	   BMS_ERROR("Create Task Fail");
   }
}

/*
 * 函数作用：电池监控任务入口，周期执行采样调度和系统模式判断。
 */
/**
 * @brief 电池监控任务入口，周期执行采样调度和系统模式判断。
 * @param paramter 任务入口参数，本函数未使用。
 * @return 无返回值。
 */
static void BMS_MonitorTaskEntry(void *paramter)
{
	while (1)
	{
		BMS_MonitorBattery();
		BMS_MonitorSysMode();
		osDelay(MONITOR_TASK_PERIOD);
	}
}


/*
 * 函数作用：按不同周期采样单体电压、总压、温度和电流，并更新 BMS_MonitorData。
 */
/**
 * @brief 按各自采样周期更新单体电压、总压、温度和电流数据。
 * @param 无。
 * @return 无返回值。
 */
static void BMS_MonitorBattery(void)
{
	// 单体电芯电压
	CountCellVoltage += MONITOR_TASK_PERIOD;
	if (FlagCellVoltage == true && CountCellVoltage >= UPDATE_CELL_VOLTAGE_CYCLE)
	{
		Bms_HalMonitorCellVoltage();
		CountCellVoltage = 0;
	}
	else if (FlagCellVoltage == false)
	{
		CountCellVoltage = 0;
	}
	
	// 电池组电压
	CountBatVoltage += MONITOR_TASK_PERIOD;
	if (FlagBatVoltage == true && CountBatVoltage >= UPDAYE_BAT_VOLTAGE_CYCLE)
	{
		Bms_HalMonitorBatteryVoltage();
		CountBatVoltage = 0;		
	}
	else if (FlagBatVoltage == false)
	{
		CountBatVoltage = 0;
	}

	// 电池温度
	CountCellTemp += MONITOR_TASK_PERIOD;
	if (FlagCellTemp == true && CountCellTemp >= UPDATE_CELL_TEMP_CYCLE)
	{
		Bms_HalMonitorCellTemperature();
		CountCellTemp = 0;
	}
	else if (FlagCellTemp == false)
	{
		CountCellTemp = 0;
	}


	/* Current sampling: read CC2 once per second. If ALERT marks a fresh
	   current sample, perform the I2C read here instead of in interrupt context. */
	CountBatCurrent += MONITOR_TASK_PERIOD;
	if (FlagBatCurrent == true && CountBatCurrent >= UPDATE_BAT_CURRENT_CYCLE)
	{
		Bms_HalMonitorBatteryCurrent();
		CountBatCurrent = 0;
		FlagSampleIntCur = false;
	}
	else if (FlagBatCurrent == false)
	{
		CountBatCurrent = 0;
		FlagSampleIntCur = false;
	}

	if (FlagSampleIntCur == true && FlagBatCurrent == true)
	{
		Bms_HalMonitorBatteryCurrent();
		FlagSampleIntCur = false;
	}
}



// BatteryCurrent > 20mA || BatteryCurrent < -20mA  处于非睡眠模式
// BatteryCurrent < 20mA || BatteryCurrent > -20mA  处于待机模式或者睡眠模式
// BatteryCurrent <= -20mA 处于放电模式
// BatteryCurrent >=  20mA 处于充电模式
/*
 * 函数作用：根据电池电流判断系统处于待机、充电、放电或睡眠模式。
 */
/**
 * @brief 根据电池电流判断系统处于待机、充电、放电或睡眠模式。
 * @param 无。
 * @return 无返回值。
 */
static void BMS_MonitorSysMode(void)
{
	static BMS_SysModeTypedef SysModeBackup = BMS_MODE_NULL;
	static uint32_t StandbyCount = 0;
	
	if (BMS_GlobalParam.SysMode == BMS_MODE_SLEEP)
	{
		if ((BMS_MonitorData.BatteryCurrent >= BMS_CURRENT_MODE_THRESHOLD_A) || (BMS_MonitorData.BatteryCurrent <= -BMS_CURRENT_MODE_THRESHOLD_A))
		{
			// 可以加唤醒处理逻辑
			
			BMS_GlobalParam.SysMode = BMS_MODE_STANDBY;
			BMS_INFO("Wake Up");
		}
		return;
	}

	
	if (BMS_MonitorData.BatteryCurrent < BMS_CURRENT_MODE_THRESHOLD_A && BMS_MonitorData.BatteryCurrent > -BMS_CURRENT_MODE_THRESHOLD_A)
	{
		BMS_GlobalParam.SysMode = BMS_MODE_STANDBY;	
		
		if (StandbyCount >= BMS_ENTRY_SLEEP_TIME * 60000)
		{
			// 没有电芯正在均衡的情况下才进入睡眠
			if (BalanceSem != NULL && osSemaphoreAcquire(BalanceSem, 0) == osOK)
			{
				// 可以加睡眠低功耗处理逻辑
				
				StandbyCount = 0;
				
				BMS_GlobalParam.SysMode = BMS_MODE_SLEEP;

				osSemaphoreRelease(BalanceSem);
				
				BMS_INFO("Entry Sleep Mode");
			}
		}
		else
		{
			StandbyCount += MONITOR_TASK_PERIOD;
		}


		// 调试用
		if (SysModeBackup != BMS_MODE_STANDBY)
		{
			SysModeBackup = BMS_MODE_STANDBY;
			BMS_INFO("Entry Standby Mode");
		}
	}
	else if (BMS_MonitorData.BatteryCurrent >= BMS_CURRENT_MODE_THRESHOLD_A)
	{
		StandbyCount = 0;
		BMS_GlobalParam.SysMode = BMS_MODE_CHARGE;

		// 调试用
		if (SysModeBackup != BMS_MODE_CHARGE)
		{
			SysModeBackup = BMS_MODE_CHARGE;
			BMS_INFO("Entry Charge Mode");
		}
	}
	else if (BMS_MonitorData.BatteryCurrent <= -BMS_CURRENT_MODE_THRESHOLD_A)
	{
		StandbyCount = 0;
		BMS_GlobalParam.SysMode = BMS_MODE_DISCHARGE;

		// 调试用
		if (SysModeBackup != BMS_MODE_DISCHARGE)
		{
			SysModeBackup = BMS_MODE_DISCHARGE;
			BMS_INFO("Entry Discharge Mode");
		}
	}
}










/*
 * 函数作用：使能或暂停单体电芯电压采样。
 */
/**
 * @brief 使能或暂停单体电芯电压采样。
 * @param NewState 目标使能状态。
 * @return 无返回值。
 */
void BMS_MonitorStateCellVoltage(BMS_StateTypedef NewState)
{
	if (NewState == BMS_STATE_ENABLE)
	{
		FlagCellVoltage = true;
	}
	else if (NewState == BMS_STATE_DISABLE)
	{
		FlagCellVoltage = false;
	}
}

/*
 * 函数作用：使能或暂停电池包总电压采样。
 */
/**
 * @brief 使能或暂停电池包总电压采样。
 * @param NewState 目标使能状态。
 * @return 无返回值。
 */
void BMS_MonitorStateBatVoltage(BMS_StateTypedef NewState)
{
	if (NewState == BMS_STATE_ENABLE)
	{
		FlagBatVoltage = true;
	}
	else if (NewState == BMS_STATE_DISABLE)
	{
		FlagBatVoltage = false;
	}
}

/*
 * 函数作用：使能或暂停电芯温度采样。
 */
/**
 * @brief 使能或暂停电芯温度采样。
 * @param NewState 目标使能状态。
 * @return 无返回值。
 */
void BMS_MonitorStateCellTemp(BMS_StateTypedef NewState)
{
	if (NewState == BMS_STATE_ENABLE)
	{
		FlagCellTemp = true;
	}
	else if (NewState == BMS_STATE_DISABLE)
	{
		FlagCellTemp = false;
	}	
}

/*
 * 函数作用：使能或暂停电池包电流采样。
 */
/**
 * @brief 使能或暂停电池包电流采样。
 * @param NewState 目标使能状态。
 * @return 无返回值。
 */
void BMS_MonitorStateBatCurrent(BMS_StateTypedef NewState)
{
	if (NewState == BMS_STATE_ENABLE)
	{
		FlagBatCurrent = true;
	}
	else if (NewState == BMS_STATE_DISABLE)
	{
		FlagBatCurrent = false;
	}	
}





/*
 * 函数作用：硬件电流采样中断回调入口，置位标志让监控任务读取最新电流。
 */
/**
 * @brief 响应 AFE 电流采样中断，通知监控任务尽快读取最新电流。
 * @param 无。
 * @return 无返回值。
 */
void BMS_MonitorHwCurrent(void)
{
	FlagSampleIntCur = true;
}
