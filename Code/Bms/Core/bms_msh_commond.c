#include "cmsis_os2.h"
#include <stdio.h>

#define MSH_CMD_EXPORT(command, desc)

#include "bms_hal_control.h"
#include "bms_hal_monitor.h"

#include "bms_monitor.h"
#include "bms_energy.h"
#include "bms_global.h"
#include "bms_debug.h"




/***************************************** 控制类 *********************************/

/* 睡眠唤醒由命令控制不合理，应该由系统控制
static void BMS_CmdWakeup(void)
{
	BMS_HalCtrlWakeup();
}
MSH_CMD_EXPORT(BMS_CmdWakeup, wakeup);


static void BMS_CmdSleep(void)
{
	BMS_HalCtrlSleep();
}
MSH_CMD_EXPORT(BMS_CmdSleep, sleep);
*/


/*
 * 函数作用：调试命令，设置全局放电开关为使能，实际开关由能量管理任务执行。
 */
/**
 * @brief 调试命令，使能全局放电许可，实际开通由能量管理任务执行。
 * @param 无。
 * @return 无返回值。
 */
static void BMS_CmdOpenDSG(void)
{
	BMS_GlobalParam.Discharge = BMS_STATE_ENABLE;
}
MSH_CMD_EXPORT(BMS_CmdOpenDSG, Open DSG);



/*
 * 函数作用：调试命令，设置全局放电开关为禁用，实际关闭由能量管理任务执行。
 */
/**
 * @brief 调试命令，禁用全局放电许可，实际关断由能量管理任务执行。
 * @param 无。
 * @return 无返回值。
 */
static void BMS_CmdCloseDSG(void)
{
	BMS_GlobalParam.Discharge = BMS_STATE_DISABLE;
}
MSH_CMD_EXPORT(BMS_CmdCloseDSG, Close DSG);



/*
 * 函数作用：调试命令，设置全局充电开关为使能，实际开关由能量管理任务执行。
 */
/**
 * @brief 调试命令，使能全局充电许可，实际开通由能量管理任务执行。
 * @param 无。
 * @return 无返回值。
 */
static void BMS_CmdOpenCHG(void)
{
	BMS_GlobalParam.Charge = BMS_STATE_ENABLE;
}
MSH_CMD_EXPORT(BMS_CmdOpenCHG, Open CHG);



/*
 * 函数作用：调试命令，设置全局充电开关为禁用，实际关闭由能量管理任务执行。
 */
/**
 * @brief 调试命令，禁用全局充电许可，实际关断由能量管理任务执行。
 * @param 无。
 * @return 无返回值。
 */
static void BMS_CmdCloseCHG(void)
{
	BMS_GlobalParam.Charge = BMS_STATE_DISABLE;
}
MSH_CMD_EXPORT(BMS_CmdCloseCHG, Close CHG);





/*
 * 函数作用：调试命令，使能电芯均衡功能，后续由能量管理任务判断是否启动均衡。
 */
/**
 * @brief 调试命令，使能电芯均衡功能。
 * @param 无。
 * @return 无返回值。
 */
static void BMS_CmdOpenBalance(void)
{
	BMS_GlobalParam.Balance = BMS_STATE_ENABLE;
}
MSH_CMD_EXPORT(BMS_CmdOpenBalance, Open Balance);



/*
 * 函数作用：调试命令，释放均衡信号量并禁用电芯均衡功能。
 */
/**
 * @brief 调试命令，停止并禁用电芯均衡功能。
 * @param 无。
 * @return 无返回值。
 */
static void BMS_CmdCloseBalance(void)
{
	osSemaphoreRelease(BalanceSem);
	BMS_GlobalParam.Balance = BMS_STATE_DISABLE;
}
MSH_CMD_EXPORT(BMS_CmdCloseBalance, Close Balance);





/*
 * 函数作用：调试命令，调用硬件负载检测并打印检测结果。
 */
/**
 * @brief 调试命令，执行负载检测并打印检测结果。
 * @param 无。
 * @return 无返回值。
 */
static void BMS_CmdLoadDetect(void)
{
	if (Bms_HalMonitorLoadDetect() == true)
	{
		BMS_INFO("Load Detected");
	}
	else
	{		
		BMS_INFO("No Load Was Detected");
	}
}
MSH_CMD_EXPORT(BMS_CmdLoadDetect, Load Detect);


/**********************************************************************************/










/***************************************** 传感数据 *********************************/

/**********************************************************************************/


