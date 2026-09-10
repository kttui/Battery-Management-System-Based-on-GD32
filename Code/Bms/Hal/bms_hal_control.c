#include "bms_hal_control.h"



#include <stdbool.h>
#include "drv_softi2c_gd30bm2016.h"




static bool ChargeOutputValid = false;
static bool DischargeOutputValid = false;
static bool PrechargeOutputValid = false;
static bool PredischargeOutputValid = false;
static BMS_StateTypedef ChargeOutputState = BMS_STATE_DISABLE;
static BMS_StateTypedef DischargeOutputState = BMS_STATE_DISABLE;
static BMS_StateTypedef PrechargeOutputState = BMS_STATE_DISABLE;
static BMS_StateTypedef PredischargeOutputState = BMS_STATE_DISABLE;

/**
 * @brief 将BMS使能状态转换为GD30驱动状态。
 * @param NewState 目标使能状态。
 * @return 返回对应的 GD30_STATE_ENABLE 或 GD30_STATE_DISABLE。
 */
static GD30BM2016_StateTypedef BMS_HalCtrlToGdState(BMS_StateTypedef NewState)
{
	return (NewState == BMS_STATE_ENABLE) ? GD30_STATE_ENABLE : GD30_STATE_DISABLE;
}


// BMS唤醒
/*
 * 函数作用：唤醒 GD30BM2016 前端芯片，使 BMS 从低功耗或休眠相关状态恢复工作。
 */
/**
 * @brief 唤醒 GD30BM2016，并使各输出状态缓存等待重新同步。
 * @param 无。
 * @return 无返回值。
 */
void BMS_HalCtrlWakeup(void)
{
	GD30BM2016_Wakeup();
	ChargeOutputValid = false;
	DischargeOutputValid = false;
	PrechargeOutputValid = false;
	PredischargeOutputValid = false;
}

// 控制BMS进入睡眠模式
/*
 * 函数作用：控制 GD30BM2016 进入低功耗状态，用于 BMS 休眠或关断场景。
 */
/**
 * @brief 控制 GD30BM2016 进入深度休眠状态。
 * @param 无。
 * @return 无返回值。
 */
void BMS_HalCtrlSleep(void)
{
	GD30BM2016_EntryShip();
}

// 控制放电状态
/*
 * 函数作用：控制放电 MOS 管状态，NewState 为使能时打开 DSG，为禁用时关闭 DSG。
 */
/**
 * @brief 控制放电 MOS 通路，并过滤重复的状态设置。
 * @param NewState 目标使能状态。
 * @return 无返回值。
 */
void BMS_HalCtrlDischarge(BMS_StateTypedef NewState)
{
	if (DischargeOutputValid == true && DischargeOutputState == NewState)
	{
		return;
	}

	GD30BM2016_ControlDSGOrCHG(GD30_DSG_CONTROL, BMS_HalCtrlToGdState(NewState));
	DischargeOutputState = NewState;
	DischargeOutputValid = true;
}

// 控制充电状态
/*
 * 函数作用：控制充电 MOS 管状态，NewState 为使能时打开 CHG，为禁用时关闭 CHG。
 */
/**
 * @brief 控制充电 MOS 通路，并过滤重复的状态设置。
 * @param NewState 目标使能状态。
 * @return 无返回值。
 */
void BMS_HalCtrlCharge(BMS_StateTypedef NewState)
{
	if (ChargeOutputValid == true && ChargeOutputState == NewState)
	{
		return;
	}

	GD30BM2016_ControlDSGOrCHG(GD30_CHG_CONTROL, BMS_HalCtrlToGdState(NewState));
	ChargeOutputState = NewState;
	ChargeOutputValid = true;
}

/**
 * @brief 控制GD30预放通路状态。
 * @param NewState 目标使能状态。
 * @return 无返回值。
 */
void BMS_HalCtrlPredischarge(BMS_StateTypedef NewState)
{
	if (PredischargeOutputValid == true && PredischargeOutputState == NewState)
	{
		return;
	}

	GD30BM2016_ControlDSGOrCHG(GD30_PDSG_CONTROL, BMS_HalCtrlToGdState(NewState));
	PredischargeOutputState = NewState;
	PredischargeOutputValid = true;
}

/**
 * @brief 控制GD30预充通路状态。
 * @param NewState 目标使能状态。
 * @return 无返回值。
 */
void BMS_HalCtrlPrecharge(BMS_StateTypedef NewState)
{
	if (PrechargeOutputValid == true && PrechargeOutputState == NewState)
	{
		return;
	}

	GD30BM2016_ControlDSGOrCHG(GD30_PCHG_CONTROL, BMS_HalCtrlToGdState(NewState));
	PrechargeOutputState = NewState;
	PrechargeOutputValid = true;
}

// 控制单节或多节电芯均衡,最多可以支持32节
/*
 * 函数作用：控制指定电芯的均衡开关，CellIndex 可表示单节、多节或全部电芯。
 */
/**
 * @brief 控制指定电芯的均衡开关，CellIndex 可表示单节、多节或全部电芯。
 * @param CellIndex 目标电芯位掩码。
 * @param NewState 目标使能状态。
 * @return 无返回值。
 */
void BMS_HalCtrlCellsBalance(BMS_CellIndexTypedef CellIndex, BMS_StateTypedef NewState)
{
	GD30BM2016_CellBalanceControl((GD30BM2016_CellIndexTypedef)CellIndex, BMS_HalCtrlToGdState(NewState));
}

