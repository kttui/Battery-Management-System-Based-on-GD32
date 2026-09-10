#include "bms_hal_monitor.h"

#include <string.h>

#include "bms_monitor.h"
#include "bms_global.h"
#include "bms_utils.h"

#include "bms_debug.h"

#include "drv_softi2c_gd30bm2016.h"





// 冒泡排序的比较程序,对电压数据进行比较
/*
 * 函数作用：比较两个电芯电压数据，用于按电压从低到高排序 CellData。
 */
/**
 * @brief 比较两个电芯电压数据，用于按电压从低到高排序 CellData。
 * @param e1 第一个待比较元素。
 * @param e2 第二个待比较元素。
 * @return 大于0表示前者应排在后者之后，其他值表示无需交换。
 */
static int compaer_cell(void *e1, void *e2)
{
	float temp1, temp2;
	
	temp1 = (*(BMS_CellDataTypedef *)e1).CellVoltage;
	temp2 = (*(BMS_CellDataTypedef *)e2).CellVoltage;

	if (temp1 > temp2)
	{
		return 1;
	}

    return 0;
}


/*
 * 函数作用：从 GD30BM2016 读取各串电芯电压，更新监控数据，并按电压大小排序。
 */

static const float BMS_CellVoltageOffset[BMS_CELL_MAX] = {
	BMS_CELL1_VOLTAGE_OFFSET_V,
	BMS_CELL2_VOLTAGE_OFFSET_V,
	BMS_CELL3_VOLTAGE_OFFSET_V,
	BMS_CELL4_VOLTAGE_OFFSET_V,
	BMS_CELL5_VOLTAGE_OFFSET_V,
};

static float BMS_CurrentZeroOffsetA = BMS_CURRENT_ZERO_OFFSET_A;
static float BMS_CurrentZeroSampleSum = 0.0f;
static float BMS_CurrentZeroSampleMin = 0.0f;
static float BMS_CurrentZeroSampleMax = 0.0f;
static uint32_t BMS_CurrentZeroSampleCount = 0U;
static uint32_t BMS_CurrentSampleSequence = 0U;
static uint32_t BMS_CurrentZeroLastSequence = 0U;

/**
 * @brief 应用全局及单串偏移校准单体电压。
 * @param index 电芯通道索引。
 * @param voltage AFE 读取的原始单体电压，单位为伏特。
 * @return 返回应用增益和偏移校准后的单体电压，单位为伏特。
 */
static float BMS_HalCalibrateCellVoltage(uint8_t index, float voltage)
{
	voltage = voltage * BMS_CELL_VOLTAGE_CAL_GAIN + BMS_CELL_VOLTAGE_CAL_OFFSET_V;

	if (index < BMS_CELL_MAX)
	{
		voltage += BMS_CellVoltageOffset[index];
	}

	return voltage;
}

/**
 * @brief 应用增益和偏移校准电池包总压。
 * @param voltage AFE 读取的原始电池包电压，单位为伏特。
 * @return 返回校准后的电池包电压，单位为伏特。
 */
static float BMS_HalCalibrateBatteryVoltage(float voltage)
{
	return voltage * BMS_BATTERY_VOLTAGE_CAL_GAIN + BMS_BATTERY_VOLTAGE_CAL_OFFSET_V;
}
/**
 * @brief 判断单体电压是否处于配置的有效范围。
 * @param voltage 待检查的单体电压，单位为伏特。
 * @return 电压处于有效范围时返回 true，否则返回 false。
 */
static bool BMS_HalIsValidCellVoltage(float voltage)
{
	return (voltage >= BMS_CELL_VALID_VOLT_MIN && voltage <= BMS_CELL_VALID_VOLT_MAX);
}

/**
 * @brief 应用方向、零点、增益和死区校准电池电流。
 * @param current AFE 读取的原始电池电流，单位为安培。
 * @return 返回完成方向、零点、增益和死区校准后的电流，单位为安培。
 */
static float BMS_HalCalibrateCurrent(float current)
{
	current = (current - BMS_CurrentZeroOffsetA) * BMS_CURRENT_DIRECTION * BMS_CURRENT_CAL_GAIN;

	if (current < BMS_CURRENT_DEADBAND_A && current > -BMS_CURRENT_DEADBAND_A)
	{
		return 0.0f;
	}

	return current;
}

/**
 * @brief 在已知功率通道关闭、物理电流应为零时学习运行时电流零点。
 * @param known_zero_current true表示外部逻辑确认当前应为零电流，false表示禁止学习并清空本轮样本。
 * @return 无返回值。
 */
void Bms_HalCurrentZeroCalibrate(bool known_zero_current)
{
#if (BMS_CURRENT_ZERO_AUTO_ENABLE != 0)
	float raw_current = GD30BM2016_SampleData.BatteryCurrent;
	float fixed_error = raw_current - BMS_CURRENT_ZERO_OFFSET_A;

	if (BMS_CurrentZeroLastSequence == BMS_CurrentSampleSequence)
	{
		return;
	}
	BMS_CurrentZeroLastSequence = BMS_CurrentSampleSequence;

	if (fixed_error < 0.0f)
	{
		fixed_error = -fixed_error;
	}

	if (known_zero_current == false ||
		fixed_error > BMS_CURRENT_ZERO_AUTO_MAX_OFFSET_A)
	{
		BMS_CurrentZeroSampleCount = 0U;
		BMS_CurrentZeroSampleSum = 0.0f;
		return;
	}

	if (BMS_CurrentZeroSampleCount == 0U)
	{
		BMS_CurrentZeroSampleMin = raw_current;
		BMS_CurrentZeroSampleMax = raw_current;
	}
	else
	{
		if (raw_current < BMS_CurrentZeroSampleMin)
		{
			BMS_CurrentZeroSampleMin = raw_current;
		}
		if (raw_current > BMS_CurrentZeroSampleMax)
		{
			BMS_CurrentZeroSampleMax = raw_current;
		}
	}

	if ((BMS_CurrentZeroSampleMax - BMS_CurrentZeroSampleMin) >
		BMS_CURRENT_ZERO_AUTO_MAX_SPAN_A)
	{
		BMS_CurrentZeroSampleCount = 1U;
		BMS_CurrentZeroSampleSum = raw_current;
		BMS_CurrentZeroSampleMin = raw_current;
		BMS_CurrentZeroSampleMax = raw_current;
		return;
	}

	BMS_CurrentZeroSampleSum += raw_current;
	BMS_CurrentZeroSampleCount++;

	if (BMS_CurrentZeroSampleCount >= BMS_CURRENT_ZERO_AUTO_SAMPLE_COUNT)
	{
		float measured_zero = BMS_CurrentZeroSampleSum / (float)BMS_CurrentZeroSampleCount;

		BMS_CurrentZeroOffsetA += (measured_zero - BMS_CurrentZeroOffsetA) *
							  BMS_CURRENT_ZERO_AUTO_FILTER_GAIN;
		BMS_INFO("Current zero calibrated raw=%.4fA offset=%.4fA",
				 measured_zero,
				 BMS_CurrentZeroOffsetA);
		BMS_CurrentZeroSampleCount = 0U;
		BMS_CurrentZeroSampleSum = 0.0f;
	}
#else
	(void)known_zero_current;
#endif
}

/**
 * @brief 获取当前生效的电流零点偏移，供调试和产线标定检查使用。
 * @param 无。
 * @return 当前电流零点偏移，单位为安培。
 */
float Bms_HalCurrentZeroOffsetGet(void)
{
	return BMS_CurrentZeroOffsetA;
}

/**
 * @brief 判断温度采样是否有效。
 * @param temp 待检查的温度值，单位为摄氏度。
 * @return 温度采样有效时返回 true，否则返回 false。
 */
static bool BMS_HalIsValidTemperature(float temp)
{
	if (temp < BMS_TEMP_MEASURE_MIN || temp > BMS_TEMP_MEASURE_MAX)
	{
		return false;
	}
#if (BMS_TEMP_ZERO_IS_INVALID != 0)
	if (temp == 0.0f)
	{
		return false;
	}
#endif
	return true;
}
/**
 * @brief 读取、校准、过滤并排序各串单体电压。
 * @param 无。
 * @return 无返回值。
 */
void Bms_HalMonitorCellVoltage(void)
{
	uint8_t valid_count = 0;

	GD30BM2016_UpdateCellVolt();
	for (uint8_t index = 0; index < BMS_GlobalParam.Cell_Real_Number; index++)
	{
		float voltage = BMS_HalCalibrateCellVoltage(index, GD30BM2016_SampleData.CellVoltage[index]);

		if (BMS_HalIsValidCellVoltage(voltage) == true)
		{
			BMS_MonitorData.CellData[valid_count].CellVoltage = voltage;
			BMS_MonitorData.CellData[valid_count].CellNumber = index;
			valid_count++;
		}
		else
		{
			voltage = 0.0f;
		}

		BMS_MonitorData.CellVoltage[index] = voltage;
	}

	for (uint8_t index = valid_count; index < BMS_CELL_MAX; index++)
	{
		BMS_MonitorData.CellData[index].CellVoltage = 0.0f;
		BMS_MonitorData.CellData[index].CellNumber = index;
	}

	BMS_MonitorData.CellVoltageEffectiveNumber = valid_count;
	BubbleSort(BMS_MonitorData.CellData, valid_count, sizeof(BMS_CellDataTypedef), compaer_cell);
}


/*
 * 函数作用：从 GD30BM2016 读取电池包总电压，并写入 BMS_MonitorData。
 */
/**
 * @brief 从 GD30BM2016 读取并校准电池包总电压，随后写入监控数据。
 * @param 无。
 * @return 无返回值。
 */
void Bms_HalMonitorBatteryVoltage(void)
{
	GD30BM2016_UpdateBatVolt();
	BMS_MonitorData.BatteryVoltage = BMS_HalCalibrateBatteryVoltage(GD30BM2016_SampleData.BatteryVoltage);
}


/*
 * 函数作用：从 GD30BM2016 读取电池包电流，并写入 BMS_MonitorData。
 */
/**
 * @brief 从 GD30BM2016 读取并校准电池包电流，随后写入监控数据。
 * @param 无。
 * @return 无返回值。
 */
void Bms_HalMonitorBatteryCurrent(void)
{
	GD30BM2016_UpdateCurrent();
	BMS_MonitorData.BatteryCurrent = BMS_HalCalibrateCurrent(GD30BM2016_SampleData.BatteryCurrent);
	BMS_CurrentSampleSequence++;
}


/*
 * 函数作用：读取温度采样值，过滤无效温度后排序，并记录有效温度通道数量。
 */
/**
 * @brief 读取各温度通道，过滤无效值、排序并记录有效通道数。
 * @param 无。
 * @return 无返回值。
 */
void Bms_HalMonitorCellTemperature(void)
{
	uint8_t index1 = 0, index2 = 0;
	
	GD30BM2016_UpdateTsTemp();	
	for (; index1 < BMS_GlobalParam.Temp_Real_Number; index1++)
	{
		float temp = GD30BM2016_SampleData.TsxTemperature[index1];

		if (BMS_HalIsValidTemperature(temp) == true)
		{
			BMS_MonitorData.CellTemp[index2++] = temp;
		}
	}
	
	BMS_MonitorData.CellTempEffectiveNumber = index2;
	for (; index2 < BMS_TEMP_MAX; index2++)
	{
		BMS_MonitorData.CellTemp[index2] = BMS_TEMP_INVALID_VALUE;
	}

	// sort valid temperatures only
	BubbleFloat(BMS_MonitorData.CellTemp, BMS_MonitorData.CellTempEffectiveNumber);
}


/*
 * 函数作用：调用 GD30BM2016 的负载检测功能，判断放电端是否检测到外部负载。
 */
/**
 * @brief 调用 GD30BM2016 负载检测功能判断放电端是否接有负载。
 * @param 无。
 * @return 检测到负载时返回 true，否则返回 false。
 */
bool Bms_HalMonitorLoadDetect(void)
{
	return GD30BM2016_LoadDetect();
}
