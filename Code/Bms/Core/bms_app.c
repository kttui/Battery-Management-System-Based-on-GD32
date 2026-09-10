#include "bms_app.h"


#include "bms_hal_control.h"
#include "bms_hal_monitor.h"

#include "bms_monitor.h"
#include "bms_protect.h"
#include "bms_analysis.h"
#include "bms_energy.h"
#include "bms_info.h"
#include "bms_comm.h"
#include "bms_global.h"

#include "drv_soft_i2c.h"
#include "drv_softi2c_gd30bm2016.h"



/*
 * 函数作用：初始化整个 BMS 系统。
 * 先配置 GD30BM2016 的硬件报警回调和保护参数，再初始化 I2C/AFE 前端，最后创建监控、保护、分析、能量和信息任务。
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

	// 使用硬件中断通知,如果烧写程序后必须重新上下电一次BQ芯片或者复位
	InitData.AlertOps.cc 	 = BMS_MonitorHwCurrent;
	//InitData.AlertOps.cc 	 = NULL;

	// 这两个中断会造成系统故障
	// 第一个报警时设备故障,表示BQ芯片有问题了
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
#endif

	// 软件初始化
	BMS_MonitorInit();	// 电池监控初始化
	BMS_ProtectInit();	// 电池保护初始化
	BMS_AnalysisInit();	// 电池分析初始化
	BMS_EnergyInit();	// 能量管理初始化
	BMS_InfoInit();		// 信息管理初始化
	BMS_CommInit();		// 通信管理初始化
	return true;
}


