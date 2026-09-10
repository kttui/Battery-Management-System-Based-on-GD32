#define BMS_DBG_TAG "Info"

#include <stdio.h>
#include "cmsis_os2.h"

#include "main.h"

#include "bms_info.h"

#include "bms_hal_monitor.h"
#include "bms_hal_control.h"

#include "bms_monitor.h"
#include "bms_energy.h"
#include "bms_analysis.h"
#include "bms_global.h"
#include "bms_debug.h"




// thread config
#define INFO_TASK_STACK_SIZE	1536	
#define INFO_TASK_PRIORITY		13
#define INFO_TASK_TIMESLICE		25

#define INFO_TASK_PERIOD		2000
#define INFO_LED_PERIOD			500


const osThreadAttr_t infoTask_attributes = {
  .name = "infoTask",
  .stack_size = INFO_TASK_STACK_SIZE,
  .priority = (osPriority_t) osPriorityNormal1,
};




static void BMS_InfoTaskEntry(void *paramter);

static void BMS_InfoPrintf(void);
static void BMS_BattLow(void);




/*
 * 函数作用：创建信息输出任务，周期打印 BMS 运行数据并刷新 SOC 指示灯。
 */
/**
 * @brief 创建信息输出任务，用于刷新 SOC 指示灯并打印运行数据。
 * @param 无。
 * @return 无返回值。
 */
void BMS_InfoInit(void)
{
	osThreadId_t thread;


	thread = osThreadNew(BMS_InfoTaskEntry, NULL, &infoTask_attributes);
	if (thread == NULL)
	{
		BMS_ERROR("Create Task Fail");
	}
}



/*
 * 函数作用：信息任务入口，按固定周期调用打印函数输出 BMS 状态。
 */
/**
 * @brief 信息任务入口，周期刷新 SOC 指示灯并输出 BMS 运行数据。
 * @param paramter 任务入口参数，本函数未使用。
 * @return 无返回值。
 */
static void BMS_InfoTaskEntry(void *paramter)
{
	uint32_t print_elapsed = INFO_TASK_PERIOD;

	while(1)
	{
		BMS_BattLow();
		if (print_elapsed >= INFO_TASK_PERIOD)
		{
			BMS_InfoPrintf();
			print_elapsed = 0U;
		}

		osDelay(INFO_LED_PERIOD);
		print_elapsed += INFO_LED_PERIOD;
	}
}


/*
 * 函数作用：根据 SOC 控制 PB6-PB9 电量指示灯显示。
 */
/**
 * @brief 控制指定位置的 SOC 电量指示灯。
 * @param index 指示灯索引，取值 0～3。
 * @param on true 表示点亮，false 表示熄灭。
 * @return 无返回值。
 */
static void BMS_SetSocLed(uint8_t index, bool on)
{
	switch (index)
	{
		case 0U:
			(on == true) ? gpio_bit_reset(LED4_GPIO_Port, LED4_Pin) : gpio_bit_set(LED4_GPIO_Port, LED4_Pin);
		break;

		case 1U:
			(on == true) ? gpio_bit_reset(LED3_GPIO_Port, LED3_Pin) : gpio_bit_set(LED3_GPIO_Port, LED3_Pin);
		break;

		case 2U:
			(on == true) ? gpio_bit_reset(LED2_GPIO_Port, LED2_Pin) : gpio_bit_set(LED2_GPIO_Port, LED2_Pin);
		break;

		case 3U:
			(on == true) ? gpio_bit_reset(LED1_GPIO_Port, LED1_Pin) : gpio_bit_set(LED1_GPIO_Port, LED1_Pin);
		break;

		default:
		break;
	}
}

/**
 * @brief 按照稳亮数量和闪烁位置刷新SOC灯条。
 * @param steady_count 从低位开始持续点亮的指示灯数量。
 * @param blink_index 闪烁指示灯索引，-1 表示无闪烁灯。
 * @param blink_on true 表示当前闪烁相位点亮，false 表示熄灭。
 * @return 无返回值。
 */
static void BMS_SetSocLedBar(uint8_t steady_count, int8_t blink_index, bool blink_on)
{
	for (uint8_t index = 0U; index < 4U; index++)
	{
		bool on = (index < steady_count) || ((blink_index == (int8_t)index) && (blink_on == true));
		BMS_SetSocLed(index, on);
	}
}

/**
 * @brief 根据SOC和充电状态生成电量灯条显示效果。
 * @param 无。
 * @return 无返回值。
 */
static void BMS_BattLow(void)
{
	static bool blink_on = false;
	float soc = BMS_AnalysisData.SOC;
	uint8_t steady_count = 0U;
	int8_t blink_index = -1;
	bool charging = (BMS_EnergyData.OutputMode == BMS_ENERGY_OUTPUT_CHARGE) &&
				(BMS_MonitorData.BatteryCurrent >= BMS_CURRENT_MODE_THRESHOLD_A);

	if (soc < 0.0f)
	{
		soc = 0.0f;
	}
	else if (soc > 1.0f)
	{
		soc = 1.0f;
	}

	if (charging == true && soc < 1.0f)
	{
		if (soc <= 0.25f)
		{
			steady_count = 0U;
			blink_index = 0;
		}
		else if (soc <= 0.50f)
		{
			steady_count = 1U;
			blink_index = 1;
		}
		else if (soc <= 0.75f)
		{
			steady_count = 2U;
			blink_index = 2;
		}
		else
		{
			steady_count = 3U;
			blink_index = 3;
		}

		blink_on = (blink_on == false);
		BMS_SetSocLedBar(steady_count, blink_index, blink_on);
		return;
	}

	blink_on = false;
	if (soc <= 0.0f)
	{
		steady_count = 0U;
	}
	else if (soc <= 0.25f)
	{
		steady_count = 1U;
	}
	else if (soc <= 0.50f)
	{
		steady_count = 2U;
	}
	else if (soc <= 0.75f)
	{
		steady_count = 3U;
	}
	else
	{
		steady_count = 4U;
	}

	BMS_SetSocLedBar(steady_count, -1, false);
}


/*
 * 函数作用：通过调试串口打印容量、SOC、电压、电流、温度和均衡状态等运行信息。
 */
/**
 * @brief 通过调试串口打印容量、SOC、电压、电流、温度和均衡状态等运行信息。
 * @param 无。
 * @return 无返回值。
 */
static void BMS_InfoPrintf(void)
{
	uint8_t index;
	char str[64];
	const char *energy_mode = "Off";

	BMS_INFO("/*************************************************************/");
	
	// 电池包实时容量
	sprintf(str, "Battery Real Capacity = %0.1fA/H", BMS_AnalysisData.CapacityReal);
	BMS_INFO("%s", str);	

	// 25摄氏度基准下的学习容量和健康状态
	sprintf(str, "Battery Learned Capacity = %0.3fA/H", BMS_AnalysisData.CapacityLearned);
	BMS_INFO("%s", str);
	sprintf(str, "Battery SOH = %u%%", BMS_AnalysisData.SOH);
	BMS_INFO("%s", str);

	// 电池包剩余容量
	sprintf(str, "Battery Remain Capacity = %0.3fA/H", BMS_AnalysisData.CapacityRemain);
	BMS_INFO("%s", str);

	print_usart1("\r\n");

	// SOC
	sprintf(str, "Battery SOC = %0.1f%%", BMS_AnalysisData.SOC * 100);
	BMS_INFO("%s", str);

	/*
	// SOE
	sprintf(str, "Battery SOE = %0.1f%", BMS_AnalysisData.SOE * 100);
	BMS_INFO("%s", str);

	// SOP
	sprintf(str, "Battery SOP = %0.1f%", BMS_AnalysisData.SOP * 100);
	BMS_INFO("%s", str);
	*/	




	print_usart1("\r\n");


	// 单体电芯最大电压
	sprintf(str, "Cell Max Voltage = %0.3fV", BMS_AnalysisData.CellVoltMax);
	BMS_INFO("%s", str);


	// 单体电芯最小电压
	sprintf(str, "Cell Min Voltage = %0.3fV", BMS_AnalysisData.CellVoltMin);
	BMS_INFO("%s", str);

	

	// 最大电压差
	sprintf(str, "Cell Max Voltage Difference = %0.3fV", BMS_AnalysisData.MaxVoltageDifference);
	BMS_INFO("%s", str);



	// 平均电压
	sprintf(str, "Cell Average Voltage = %0.3fV", BMS_AnalysisData.AverageVoltage);
	BMS_INFO("%s", str);



	// 实时功率
	sprintf(str, "Battery Real Power = %0.3fW", BMS_AnalysisData.PowerReal);
	BMS_INFO("%s", str);

	switch (BMS_EnergyData.OutputMode)
	{
		case BMS_ENERGY_OUTPUT_PRECHARGE: energy_mode = "Precharge"; break;
		case BMS_ENERGY_OUTPUT_CHARGE: energy_mode = "Charge"; break;
		case BMS_ENERGY_OUTPUT_PREDISCHARGE: energy_mode = "Predischarge"; break;
		case BMS_ENERGY_OUTPUT_DISCHARGE: energy_mode = "Discharge"; break;
		default: break;
	}
	sprintf(str, "Energy Output Mode = %s", energy_mode);
	BMS_INFO("%s", str);




	print_usart1("\r\n");




	// Battery Voltage
	sprintf(str, "Battery Voltage = %0.3fV", BMS_MonitorData.BatteryVoltage);
	BMS_INFO("%s", str);	




	// Battery Current
	sprintf(str, "Battery Current = %0.3fA", BMS_MonitorData.BatteryCurrent);
	BMS_INFO("%s", str);	




	// Cell Temperature
	if (BMS_MonitorData.CellTempEffectiveNumber == 0U)
	{
		BMS_INFO("Temperature invalid");
	}
	for (index = 0; index < BMS_MonitorData.CellTempEffectiveNumber; index++)
	{
		sprintf(str, "Temperature %d = %0.1f", index + 1, BMS_MonitorData.CellTemp[index]);
		BMS_INFO("%s", str);
	}

	print_usart1("\r\n");

	// Cell Voltage
	for (index = 0; index < BMS_GlobalParam.Cell_Real_Number; index++)
	{
		sprintf(str, "Cell%-2d Voltage = %-5.3fV %s",
		index + 1, 
		BMS_MonitorData.CellVoltage[index],
		(BMS_EnergyData.BalanceRecord &  (1 << index)) > 0 ? "--->" : "");
		BMS_INFO("%s", str);
	}
	
	BMS_INFO("/*************************************************************/\r\n\r\n");
}



