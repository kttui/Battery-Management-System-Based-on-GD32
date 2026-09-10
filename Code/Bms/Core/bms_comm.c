#define BMS_DBG_TAG "Comm"

#include <stdio.h>
#include <stdint.h>
#include "cmsis_os2.h"

#include "main.h"

#include "bms_comm.h"

#include "bms_hal_monitor.h"
#include "bms_hal_control.h"

#include "bms_monitor.h"
#include "bms_energy.h"
#include "bms_analysis.h"
#include "bms_protect.h"
#include "bms_global.h"
#include "bms_debug.h"




#include "drv_can.h"
#include "can.h"
// thread config
#define COMM_TASK_STACK_SIZE	768	
#define COMM_TASK_PRIORITY		13
#define COMM_TASK_TIMESLICE		25

#define COMM_TASK_PERIOD		2000


const osThreadAttr_t commTask_attributes = {
  .name = "commTask",
  .stack_size = COMM_TASK_STACK_SIZE,
  .priority = (osPriority_t) osPriorityNormal1,
};






static void BMS_CommTaskEntry(void *paramter);
static void BMS_CommSendCanTelemetry(void);
static void BMS_CommSendCanCells(uint32_t id, uint8_t start_index);
static void BMS_CommPutU16(uint8_t *data, uint8_t offset, uint16_t value);
static uint16_t BMS_CommClampU16(float value, float scale);
static int16_t BMS_CommClampI16(float value, float scale);
/*
 * 函数作用：创建 BMS 通信任务，预留后续 CAN/RS485 通信协议处理入口。
 */
/**
 * @brief 初始化 CAN 驱动并创建周期遥测通信任务。
 * @param 无。
 * @return 无返回值。
 */
void BMS_CommInit(void)
{
	osThreadId_t thread;

	CAN_Initialize();

	thread = osThreadNew(BMS_CommTaskEntry, NULL, &commTask_attributes);

	if (thread == NULL)
	{
		BMS_ERROR("Create Task Fail");
	}
}


/*
 * 函数作用：通信任务入口，周期上报核心 CAN 遥测数据。
 */
/**
 * @brief 通信任务入口，周期发送 BMS 的 CAN 遥测数据。
 * @param paramter 任务入口参数，本函数未使用。
 * @return 无返回值。
 */
static void BMS_CommTaskEntry(void *paramter)
{
	(void)paramter;

	while(1)
	{
		BMS_CommSendCanTelemetry();
		osDelay(COMM_TASK_PERIOD);
	}
}
/**
 * @brief 缩放浮点数并饱和转换为无符号16位数。
 * @param value 待缩放的浮点数值。
 * @param scale 数值缩放系数。
 * @return 返回四舍五入并限制在 0～65535 范围内的结果。
 */
static uint16_t BMS_CommClampU16(float value, float scale)
{
	float scaled = value * scale;

	if (scaled <= 0.0f)
	{
		return 0U;
	}
	if (scaled >= 65535.0f)
	{
		return 65535U;
	}

	return (uint16_t)(scaled + 0.5f);
}

/**
 * @brief 缩放浮点数并饱和转换为有符号16位数。
 * @param value 待缩放的浮点数值。
 * @param scale 数值缩放系数。
 * @return 返回四舍五入并限制在 -32768～32767 范围内的结果。
 */
static int16_t BMS_CommClampI16(float value, float scale)
{
	float scaled = value * scale;

	if (scaled > 32767.0f)
	{
		return 32767;
	}
	if (scaled < -32768.0f)
	{
		return -32768;
	}

	return (int16_t)((scaled >= 0.0f) ? (scaled + 0.5f) : (scaled - 0.5f));
}

/**
 * @brief 按小端格式向CAN数据缓冲区写入16位数据。
 * @param data 目标 CAN 数据缓冲区。
 * @param offset 缓冲区字节偏移。
 * @param value 要写入的 16 位数值。
 * @return 无返回值。
 */
static void BMS_CommPutU16(uint8_t *data, uint8_t offset, uint16_t value)
{
	data[offset] = (uint8_t)(value & 0xFFU);
	data[offset + 1U] = (uint8_t)((value >> 8U) & 0xFFU);
}

/**
 * @brief 打包并发送连续四串单体电压。
 * @param id CAN帧ID。
 * @param start_index 本帧第一个单体索引。
 * @return 无返回值。
 */
static void BMS_CommSendCanCells(uint32_t id, uint8_t start_index)
{
	uint8_t data[8] = {0U};
	uint8_t index;

	for (index = 0U; index < 4U; index++)
	{
		uint8_t cell_index = start_index + index;
		uint16_t cell_mv = 0U;

		if (cell_index < BMS_CELL_MAX)
		{
			cell_mv = BMS_CommClampU16(BMS_MonitorData.CellVoltage[cell_index], 1000.0f);
		}

		BMS_CommPutU16(data, (uint8_t)(index * 2U), cell_mv);
	}

	CAN_SendData(CAN_ID_STD, id, 0U, data, sizeof(data));
}

/**
 * @brief 发送总压、电流、SOC、告警及单体电压遥测帧。
 * @param 无。
 * @return 无返回值。
 */
static void BMS_CommSendCanTelemetry(void)
{
	uint8_t data[8] = {0U};
	int16_t current_ma = BMS_CommClampI16(BMS_MonitorData.BatteryCurrent, 1000.0f);
	uint16_t alert = (uint16_t)BMS_ProtectAlert;

	BMS_CommPutU16(data, 0U, BMS_CommClampU16(BMS_MonitorData.BatteryVoltage, 1000.0f));
	BMS_CommPutU16(data, 2U, (uint16_t)current_ma);
	BMS_CommPutU16(data, 4U, BMS_CommClampU16(BMS_AnalysisData.SOC, 1000.0f));
	BMS_CommPutU16(data, 6U, alert);
	CAN_SendData(CAN_ID_STD, BMS_CAN_ID_STATUS, 0U, data, sizeof(data));

	BMS_CommSendCanCells(BMS_CAN_ID_CELL_1_4, 0U);
	BMS_CommSendCanCells(BMS_CAN_ID_CELL_5_8, 4U);
}
