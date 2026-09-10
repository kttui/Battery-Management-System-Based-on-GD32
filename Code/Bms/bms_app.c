#include "bms_app.h"
#include "cmsis_os2.h"


#include "bms_hal_control.h"
#include "bms_hal_monitor.h"

#include "bms_monitor.h"
#include "bms_protect.h"
#include "bms_analysis.h"
#include "bms_energy.h"
#include "bms_info.h"
#include "bms_comm.h"
#include "bms_charger.h"
#include "bms_global.h"
#define BMS_DBG_TAG "App"
#include "bms_debug.h"

#include "drv_soft_i2c.h"
#include "drv_softi2c_gd30bm2016.h"
/**
 * @brief 判断当前最低单体电压是否允许执行均衡通道自检。
 * @param 无。
 * @return 最低单体电压达到自检阈值时返回 true，否则返回 false。
 */
static bool BMS_BalanceLedSelfTestVoltageOk(void)
{
	if (BMS_MonitorData.CellVoltageEffectiveNumber == 0U)
	{
		return false;
	}

	return (BMS_MonitorData.CellData[0].CellVoltage >= BALANCE_LED_SELF_TEST_MIN_CELL_V);
}

/**
 * @brief 依次控制各单体均衡通道完成上电自检。
 * @param 无。
 * @return 无返回值。
 */
static void BMS_BalanceLedSelfTest(void)
{
#if (BALANCE_LED_SELF_TEST_ENABLE != 0)
	static const BMS_CellIndexTypedef cells[BMS_CELL_MAX] = {
		BMS_CELL_INDEX1,
		BMS_CELL_INDEX2,
		BMS_CELL_INDEX3,
		BMS_CELL_INDEX4,
		BMS_CELL_INDEX5,
	};
	uint8_t cell_count = BMS_GlobalParam.Cell_Real_Number;

	if (cell_count > BMS_CELL_MAX)
	{
		cell_count = BMS_CELL_MAX;
	}

	if (BMS_BalanceLedSelfTestVoltageOk() == false)
	{
		BMS_INFO("Balance LED self test skip: min=%.3fV", BMS_MonitorData.CellData[0].CellVoltage);
		return;
	}

	BMS_INFO("Balance LED self test start");
	BMS_HalCtrlCellsBalance(BMS_CELL_ALL, BMS_STATE_DISABLE);
	osDelay(200U);

	for (uint8_t index = 0U; index < cell_count; index++)
	{
		BMS_INFO("Balance LED self test Cell:%u", (unsigned int)(index + 1U));
		BMS_HalCtrlCellsBalance(cells[index], BMS_STATE_ENABLE);
		osDelay(BALANCE_LED_SELF_TEST_CELL_MS);
		BMS_HalCtrlCellsBalance(cells[index], BMS_STATE_DISABLE);
		osDelay(200U);
	}

	BMS_HalCtrlCellsBalance(BMS_CELL_ALL, BMS_STATE_DISABLE);
	BMS_INFO("Balance LED self test stop");
#endif
}



/*
 * 函数作用：初始化整个 BMS 系统。
 * 先配置 GD30BM2016 的硬件报警回调和保护参数，再初始化 I2C/AFE 前端，最后创建监控、保护、分析、能量和信息任务。
 */
/**
 * @brief 初始化AFE、首次采样并创建全部BMS业务任务。
 * @param 无。
 * @return true表示操作成功，false表示操作失败。
 */
bool BMS_SysInitialize(void)
{
	#if 1
	GD30BM2016_InitDataTypedef InitData;

	InitData.AlertOps.ocd 	 = BMS_ProtectHwOCD;
	InitData.AlertOps.scd 	 = BMS_ProtectHwSCD;
#if (BMS_OV_PROTECT_ENABLE != 0)
	InitData.AlertOps.ov	 = BMS_ProtectHwOV;
#else
	InitData.AlertOps.ov	 = 0;
#endif
	InitData.AlertOps.uv 	 = BMS_ProtectHwUV;	

	// 使用硬件中断通知,如果烧写程序后必须重新上下电一次芯片或者复位
	InitData.AlertOps.cc 	 = BMS_MonitorHwCurrent;
	//InitData.AlertOps.cc 	 = NULL;

	// 这两个中断会造成系统故障
	// 第一个报警时设备故障,表示芯片有问题了
	// 第二个报警可能存在被外界电磁信号干扰造成误判,之前出现过,换了个跟官方一样阻值的电阻就没出现过了
	InitData.AlertOps.device = BMS_ProtectHwDevice;
	InitData.AlertOps.ovrd 	 = BMS_ProtectHwOvrd;

	InitData.ConfigData.SCDDelay	 = (GD30BM2016_SCDDelayTypedef)INIT_SCD_DELAY;
	InitData.ConfigData.OCDDelay	 = (GD30BM2016_OCDDelayTypedef)INIT_OCD_DELAY;
	InitData.ConfigData.UVDelay	 	 = (GD30BM2016_UVDelayTypedef)INIT_UV_DELAY;
	InitData.ConfigData.OVDelay	 	 = (GD30BM2016_OVDelayTypedef)INIT_OV_DELAY;
	InitData.ConfigData.UVPThreshold = INIT_UV_PROTECT * 1000;
	InitData.ConfigData.OVPThreshold = INIT_OV_PROTECT * 1000;
	InitData.ConfigData.OCCThreshold = INIT_OCC_MAX;
	InitData.ConfigData.OCDThreshold = INIT_OCD_MAX;


	// 硬件初始化
	I2C_BusInitialize();
	if (GD30BM2016_Initialize(&InitData) == false)
	{
		return false;
	}

	Bms_HalMonitorCellVoltage();
	Bms_HalMonitorBatteryVoltage();
	Bms_HalMonitorCellTemperature();
	Bms_HalMonitorBatteryCurrent();

	BMS_BalanceLedSelfTest();
#endif

	// 软件初始化
	BMS_MonitorInit();	// 电池监控初始化
	BMS_ProtectInit();	// 电池保护初始化
	BMS_AnalysisInit();	// 电池分析初始化
	BMS_EnergyInit();	// 能量管理初始化
	BMS_InfoInit();		// 信息管理初始化
	BMS_CommInit();		// 通信管理初始化
	BMS_ChargerInit();	// 充电器主控 CAN 协议初始化
	return true;
}


