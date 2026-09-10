#define BMS_DBG_TAG "Analysis"

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include "cmsis_os2.h"

#include "bms_analysis.h"

#include "bms_monitor.h"
#include "bms_protect.h"
#include "bms_energy.h"
#include "bms_soc_store.h"

#include "bms_utils.h"
#include "bms_global.h"
#include "bms_debug.h"
#include "bms_hal_monitor.h"





// thread config
#define ANALYSISI_TASK_STACK_SIZE	1024
#define ANALYSISI_TASK_PRIORITY		11
#define ANALYSISI_TASK_TIMESLICE	25

#define ANALYSISI_TASK_PERIOD		1000



#define TEMP_CAP_RATE_LIMITH_HIGH   1050
#define TEMP_CAP_RATE_LIMITL_LOW    750
#define SOC_LOG_COLOR              "\033[1;35m"
#define SOC_LOG_RESET              "\033[0m"







BMS_AnalysisDataTypedef BMS_AnalysisData =
{
	.CapacityRated = BMS_BATTERY_CAPACITY,
};





static bool SocInitialized = false;
static uint32_t LastAhTick = 0U;
static uint32_t SocRestStartTick = 0U;
static uint32_t LastOcvCorrectTick = 0U;
static uint32_t SocRestVoltageWindowTick = 0U;
static float SocRestVoltageReference = 0.0f;
static bool SocRestVoltageStable = false;
static bool SocRestAnchorHandled = false;

static float SocCoulombNetAh = 0.0f;
static float SocCoulombThroughputAh = 0.0f;
static bool CapacityLearnAnchorValid = false;
static float CapacityLearnAnchorSoc = 0.0f;
static float CapacityLearnAnchorNetAh = 0.0f;
static float CapacityLearnAnchorThroughputAh = 0.0f;

static BMS_SocPersistDataTypedef SocRestoreData = {0};
static bool SocRestorePending = false;
static bool SocPersistBaselineValid = false;
static uint32_t LastSocPersistTick = 0U;
static float LastPersistedSoc = 0.0f;
static float LastPersistedCapacityAh = 0.0f;
static uint32_t LastPersistedLoopCount = 0U;
static BMS_SysModeTypedef LastPersistObservedMode = BMS_MODE_NULL;
// 三元锂电池 SOC开路电压法计算数据表
// 支持磷酸铁锂、钛酸锂也得做一张这个表
uint16_t SocOcvTab[101]=
{
	3288, // 0%~1%	
	3309, 3334, 3357, 3378, 3398, 3417, 3434, 3449, 3464, 3477,	// 0%~10%
	3489, 3500, 3510, 3520, 3528, 3536, 3543, 3549, 3555, 3561,	// 11%~20%
	3566, 3571, 3575, 3579, 3583, 3586, 3590, 3593, 3596, 3599,	// 21%~30%
	3602, 3605, 3608, 3611, 3615, 3618, 3621, 3624, 3628, 3632,	// 31%~40%
	3636, 3640, 3644, 3648, 3653, 3658, 3663, 3668, 3674, 3679,	// 41%~50%
	3685, 3691, 3698, 3704, 3711, 3718, 3725, 3733, 3741, 3748,	// 51%~60%
	3756, 3765, 3773, 3782, 3791, 3800, 3809, 3818, 3827, 3837,	// 61%~70%
	3847, 3857, 3867, 3877, 3887, 3897, 3908, 3919, 3929, 3940,	// 71%~80%
	3951, 3962, 3973, 3985, 3996, 4008, 4019, 4031, 4043, 4055,	// 81%~90%
	4067, 4080, 4092, 4105, 4118, 4131, 4145, 4158, 4172, 4185,	// 91~100%
};

const osThreadAttr_t analysisTask_attributes = {
  .name = "analysisTask",
  .stack_size = ANALYSISI_TASK_STACK_SIZE,
  .priority = (osPriority_t) osPriorityNormal3,
};


/**
 * @brief 查询SOC是否已经完成初始化。
 * @param 无。
 * @return SOC 已初始化时返回 true，否则返回 false。
 */
bool BMS_AnalysisIsSocReady(void)
{
	return SocInitialized;
}
static void BMS_AnalysisTaskEntry(void *paramter);
static uint8_t BMS_AnalysisCellCount(void);
static float BMS_AnalysisAbs(float value);
static void BMS_AnalysisClampSoc(void);
static void BMS_AnalysisSyncCapacityFromSoc(void);
static bool BMS_AnalysisHasValidCells(void);
static bool BMS_AnalysisInitSocFromOcv(void);
static float BMS_AnalysisLimit(float value, float min, float max);
static float BMS_AnalysisOcvSocValue(void);
static void BMS_AnalysisUpdateSoh(void);
static void BMS_AnalysisCapacityLearnAtRest(float ocv_soc);
static void BMS_AnalysisPersistMaybe(void);
static void BMS_AnalysisResetRestDetection(void);


/**
 * @brief 统计单体最高、最低、平均电压、最大压差和实时功率。
 * @param 无。
 * @return 无返回值。
 */
static void BMS_AnalysisEasy(void)
{
	uint8_t index;
	uint8_t cell_count = BMS_AnalysisCellCount();

	if (cell_count == 0U)
	{
		BMS_AnalysisData.MaxVoltageDifference = 0.0f;
		BMS_AnalysisData.AverageVoltage = 0.0f;
		BMS_AnalysisData.CellVoltMax = 0.0f;
		BMS_AnalysisData.CellVoltMin = 0.0f;
		BMS_AnalysisData.PowerReal = 0.0f;
		return;
	}

	BMS_AnalysisData.MaxVoltageDifference = BMS_MonitorData.CellData[cell_count - 1U].CellVoltage - BMS_MonitorData.CellData[0].CellVoltage;
	BMS_AnalysisData.AverageVoltage = 0.0f;
	for (index = 0; index < cell_count; index++)
	{
		BMS_AnalysisData.AverageVoltage += BMS_MonitorData.CellData[index].CellVoltage;
	}
	BMS_AnalysisData.AverageVoltage /= cell_count;
	
	BMS_AnalysisData.PowerReal = BMS_MonitorData.BatteryVoltage * BMS_MonitorData.BatteryCurrent;
	BMS_AnalysisData.CellVoltMax = BMS_MonitorData.CellData[cell_count - 1U].CellVoltage;
	BMS_AnalysisData.CellVoltMin = BMS_MonitorData.CellData[0].CellVoltage;
}


/**
 * @brief 根据有效温度修正电池可用容量。
 * @param 无。
 * @return 无返回值。
 */
static void BMS_AnalysisTempCal(void)
{
	uint8_t  Ratio; 	// 校准比率
	int32_t RateTemp;	
	int16_t MinTemp = BMS_MonitorData.CellTemp[0] * 10;
	float capacity_base;


	if (BMS_MonitorData.CellTempEffectiveNumber == 0)
	{
		return;
	}


	// 确定校准比率
	if (MinTemp >= 250)                             
	{
		Ratio = 1;
	}
	else if (MinTemp >= 100 && MinTemp < 250)   
	{   
		Ratio = 2;
	}
	else if (MinTemp >= 0 && MinTemp < 100)      
	{   
		Ratio = 3;
	}
	else if (MinTemp >= -200 && MinTemp < -10)    
	{   
		Ratio = 4;
	}
	else if (MinTemp >= -300 && MinTemp < -200)    
	{   
		Ratio = 5;
	}
	else
	{
		Ratio = 6;                                       
	}


	RateTemp = 1000 + Ratio * (MinTemp - 250) / 10;
	if(RateTemp > TEMP_CAP_RATE_LIMITH_HIGH )
	{
		RateTemp = TEMP_CAP_RATE_LIMITH_HIGH;
	}
	else if(RateTemp < TEMP_CAP_RATE_LIMITL_LOW)
	{
		RateTemp = TEMP_CAP_RATE_LIMITL_LOW;
	}

	capacity_base = (BMS_AnalysisData.CapacityLearned > 0.0f) ?
					BMS_AnalysisData.CapacityLearned : BMS_AnalysisData.CapacityRated;

	// 实时容量：先使用25摄氏度基准下的学习容量，再叠加温度修正。
	BMS_AnalysisData.CapacityReal = capacity_base * (float)RateTemp / 1000.0f;

	// 剩余容量
	BMS_AnalysisData.CapacityRemain = BMS_AnalysisData.CapacityReal * BMS_AnalysisData.SOC;
}



/*
 * 函数作用：执行实时容量校准，目前主要调用温度容量修正逻辑。
 */
/**
 * @brief 执行实时容量校准，目前主要调用温度容量修正逻辑。
 * @param 无。
 * @return 无返回值。
 */
static void BMS_AnalysisCalCap(void)
{
	BMS_AnalysisTempCal();
}


// 根据单体电芯最低电压计算出soc值,用于上电和长时间静止状态下的校准
/*
 * 函数作用：通过开路电压表把单体最低电压换算为 SOC 千分比值。
 */
/**
 * @brief 根据开路电压表将最低单体电压换算为 SOC 千分比。
 * @param voltage 最低单体电压，单位为毫伏。
 * @return 返回 0～1000 的 SOC 千分比值。
 */
static uint16_t BMS_AnalysisOcvToSoc(uint16_t voltage)
{
	if (voltage <= SocOcvTab[0])
	{
		return 0U;
	}
	if (voltage >= SocOcvTab[100])
	{
		return 1000U;
	}

	for (uint16_t index = 0U; index < 100U; index++)
	{
		uint16_t low = SocOcvTab[index];
		uint16_t high = SocOcvTab[index + 1U];

		if (voltage <= high)
		{
			uint16_t span = high - low;
			uint16_t offset = voltage - low;

			if (span == 0U)
			{
				return (uint16_t)(index * 10U);
			}

			return (uint16_t)((index * 10U) + ((uint32_t)offset * 10U) / span);
		}
	}

	return 1000U;
}

/*
 * 函数作用：在睡眠或静置条件下使用开路电压法校准 SOC 和剩余容量。
 */
/**
 * @brief 在满足静置条件时使用开路电压法修正 SOC 和剩余容量。
 * @param 无。
 * @return 无返回值。
 */
static void BMS_AnalysisOcvSocCalculate(void)
{
#if (SOC_EST_OCV_FUSION_ENABLE != 0)
	uint32_t now = osKernelGetTickCount();
	float current_abs;
	float min_voltage;
	float voltage_delta;
	float ocv_soc;
	float error;
	float correction;

	if (SocInitialized == false || BMS_AnalysisHasValidCells() == false)
	{
		BMS_AnalysisResetRestDetection();
		return;
	}

	current_abs = BMS_AnalysisAbs(BMS_MonitorData.BatteryCurrent);
	if (current_abs > SOC_EST_REST_CURRENT_A)
	{
		BMS_AnalysisResetRestDetection();
		return;
	}

#if (SOC_EST_REST_REQUIRE_STANDBY_MODE != 0)
	if (BMS_GlobalParam.SysMode != BMS_MODE_STANDBY &&
		BMS_GlobalParam.SysMode != BMS_MODE_SLEEP)
	{
		BMS_AnalysisResetRestDetection();
		return;
	}
#endif

	min_voltage = BMS_MonitorData.CellData[0].CellVoltage;
	if (SocRestVoltageWindowTick == 0U)
	{
		SocRestVoltageWindowTick = now;
		SocRestVoltageReference = min_voltage;
		return;
	}

	voltage_delta = BMS_AnalysisAbs(min_voltage - SocRestVoltageReference);
	if (voltage_delta > SOC_EST_REST_VOLTAGE_MAX_DELTA_V)
	{
		BMS_AnalysisResetRestDetection();
		SocRestVoltageWindowTick = now;
		SocRestVoltageReference = min_voltage;
		return;
	}

	if ((uint32_t)(now - SocRestVoltageWindowTick) >= SOC_EST_REST_VOLTAGE_WINDOW_MS)
	{
		SocRestVoltageStable = true;
		SocRestVoltageWindowTick = now;
		SocRestVoltageReference = min_voltage;
	}

	if (SocRestVoltageStable == false)
	{
		return;
	}

	if (SocRestStartTick == 0U)
	{
		SocRestStartTick = now;
		LastOcvCorrectTick = now;
		return;
	}

	if ((uint32_t)(now - SocRestStartTick) < SOC_EST_REST_TIME_MS)
	{
		return;
	}

	ocv_soc = BMS_AnalysisOcvSocValue();
	if (SocRestAnchorHandled == false)
	{
		BMS_AnalysisCapacityLearnAtRest(ocv_soc);
		SocRestAnchorHandled = true;
	}

	if ((uint32_t)(now - LastOcvCorrectTick) < SOC_EST_OCV_CORRECT_INTERVAL_MS)
	{
		return;
	}

	LastOcvCorrectTick = now;
	error = ocv_soc - BMS_AnalysisData.SOC;
	correction = error * SOC_EST_OCV_CORRECT_GAIN;
	correction = BMS_AnalysisLimit(correction, -SOC_EST_OCV_MAX_STEP, SOC_EST_OCV_MAX_STEP);

	BMS_AnalysisData.SOC += correction;
	BMS_AnalysisSyncCapacityFromSoc();
	BMS_INFO(SOC_LOG_COLOR "SOC OCV fusion ocv=%.1f%% err=%.1f%% soc=%.1f%%" SOC_LOG_RESET,
			 ocv_soc * 100.0f,
			 error * 100.0f,
			 BMS_AnalysisData.SOC * 100.0f);
#endif
}
/**
 * @brief 根据电流和时间进行安时积分并更新SOC。
 * @param 无。
 * @return 无返回值。
 */
static void BMS_AnalysisAHSocCalculate(void)
{
	float current_ah;
	float capacity_delta_ah;
	uint32_t now = osKernelGetTickCount();
	uint32_t dt_ms;

	if (SocInitialized == false || BMS_AnalysisHasValidCells() == false)
	{
		LastAhTick = now;
		return;
	}

	if (LastAhTick == 0U)
	{
		LastAhTick = now;
		return;
	}

	dt_ms = (uint32_t)(now - LastAhTick);
	LastAhTick = now;
	if (dt_ms == 0U)
	{
		return;
	}
	if (dt_ms > SOC_EST_AH_MAX_DT_MS)
	{
		dt_ms = SOC_EST_AH_MAX_DT_MS;
	}

	current_ah = BMS_MonitorData.BatteryCurrent * (float)dt_ms / (1000.0f * 3600.0f);

	if (BMS_AnalysisData.CapacityReal <= 0.0f)
	{
		BMS_AnalysisData.CapacityReal = BMS_AnalysisData.CapacityRated;
	}


	if (current_ah > 0.0f)
	{
		capacity_delta_ah = current_ah * SOC_EST_CHARGE_EFFICIENCY;
	}
	else if (current_ah < 0.0f)
	{
		capacity_delta_ah = current_ah * SOC_EST_DISCHARGE_EFFICIENCY;
	}
	else
	{
		return;
	}

	BMS_AnalysisData.CapacityRemain += capacity_delta_ah;
	SocCoulombNetAh += capacity_delta_ah;
	SocCoulombThroughputAh += BMS_AnalysisAbs(capacity_delta_ah);

	if (capacity_delta_ah < 0.0f && BMS_AnalysisData.CapacityLearned > 0.0f)
	{
		BMS_AnalysisData.CapacityLoop += -capacity_delta_ah;
		while (BMS_AnalysisData.CapacityLoop >= BMS_AnalysisData.CapacityLearned)
		{
			BMS_AnalysisData.CapacityLoop -= BMS_AnalysisData.CapacityLearned;
			BMS_AnalysisData.LoopCount++;
		}
	}

	BMS_AnalysisData.CapacityRemain = BMS_AnalysisLimit(BMS_AnalysisData.CapacityRemain,
										 0.0f,
										 BMS_AnalysisData.CapacityReal);
	BMS_AnalysisData.SOC = BMS_AnalysisData.CapacityRemain / BMS_AnalysisData.CapacityReal;
	BMS_AnalysisClampSoc();
}
/**
 * @brief 初始化SOC并执行安时积分和静置OCV修正。
 * @param 无。
 * @return 无返回值。
 */
static void BMS_AnalysisSocCheck(void)
{
	if (SocInitialized == false)
	{
		if (BMS_AnalysisInitSocFromOcv() == false)
		{
			return;
		}
	}

	BMS_AnalysisAHSocCalculate();
	BMS_AnalysisOcvSocCalculate();
}

/*
 * 函数作用：上电初始化实际容量、剩余容量和初始 SOC。
 */
/**
 * @brief 上电初始化实际容量、剩余容量和初始 SOC。
 * @param 无。
 * @return 无返回值。
 */
static void BMS_AnalysisCapAndSocInit(void)
{
	BMS_AnalysisData.SOC = 0.0f;
	BMS_AnalysisData.SOH = 100U;
	BMS_AnalysisData.LoopCount = 0U;
	BMS_AnalysisData.CapacityLoop = 0.0f;
	BMS_AnalysisData.CapacityLearned = BMS_AnalysisData.CapacityRated;
	BMS_AnalysisData.CapacityReal = BMS_AnalysisData.CapacityRated;
	BMS_AnalysisData.CapacityRemain = 0.0f;
	SocInitialized = false;
	LastAhTick = 0U;
	BMS_AnalysisResetRestDetection();
	SocCoulombNetAh = 0.0f;
	SocCoulombThroughputAh = 0.0f;
	CapacityLearnAnchorValid = false;
	SocRestorePending = false;
	SocPersistBaselineValid = false;
	LastSocPersistTick = osKernelGetTickCount();
	LastPersistObservedMode = BMS_MODE_NULL;
}
/**
 * @brief 取得当前可用于分析的有效单体数量。
 * @param 无。
 * @return 返回当前可参与分析的有效单体数量。
 */
static uint8_t BMS_AnalysisCellCount(void)
{
	uint8_t cell_count = BMS_MonitorData.CellVoltageEffectiveNumber;
	uint8_t configured_count = BMS_GlobalParam.Cell_Real_Number;

	if (configured_count == 0U || configured_count > BMS_CELL_MAX)
	{
		configured_count = BMS_CELL_MAX;
	}

	if (cell_count > configured_count)
	{
		cell_count = configured_count;
	}

	return cell_count;
}

/**
 * @brief 判断是否存在可用于SOC分析的有效单体。
 * @param 无。
 * @return 存在至少一节有效单体时返回 true，否则返回 false。
 */
static bool BMS_AnalysisHasValidCells(void)
{
	uint8_t configured_count = BMS_GlobalParam.Cell_Real_Number;

	if (configured_count == 0U || configured_count > BMS_CELL_MAX)
	{
		configured_count = BMS_CELL_MAX;
	}

	return (BMS_AnalysisCellCount() == configured_count);
}

/**
 * @brief 使用最低单体OCV初始化SOC和剩余容量。
 * @param 无。
 * @return true表示操作成功，false表示操作失败。
 */
static bool BMS_AnalysisInitSocFromOcv(void)
{
	uint16_t soc_permille;
	float ocv_soc;
	float min_capacity;
	float max_capacity;

	if (BMS_AnalysisHasValidCells() == false)
	{
		return false;
	}

	soc_permille = BMS_AnalysisOcvToSoc((uint16_t)(BMS_MonitorData.CellData[0].CellVoltage * 1000.0f));
	ocv_soc = soc_permille / 1000.0f;
	min_capacity = BMS_AnalysisData.CapacityRated * SOC_EST_CAPACITY_MIN_RATIO;
	max_capacity = BMS_AnalysisData.CapacityRated * SOC_EST_CAPACITY_MAX_RATIO;

	if (SocRestorePending == true &&
		SocRestoreData.CapacityLearnedAh >= min_capacity &&
		SocRestoreData.CapacityLearnedAh <= max_capacity &&
		BMS_AnalysisAbs(SocRestoreData.Soc - ocv_soc) <= SOC_EST_RESTORE_MAX_OCV_DIFF)
	{
		BMS_AnalysisData.SOC = SocRestoreData.Soc;
		BMS_AnalysisData.CapacityLearned = SocRestoreData.CapacityLearnedAh;
		BMS_AnalysisData.CapacityLoop = SocRestoreData.CapacityLoopAh;
		BMS_AnalysisData.LoopCount = SocRestoreData.LoopCount;
		BMS_AnalysisUpdateSoh();
		BMS_AnalysisTempCal();
		SocPersistBaselineValid = true;
		LastPersistedSoc = BMS_AnalysisData.SOC;
		LastPersistedCapacityAh = BMS_AnalysisData.CapacityLearned;
		LastPersistedLoopCount = BMS_AnalysisData.LoopCount;
		BMS_INFO("SOC restored %.1f%% learned=%.3fAh SOH=%u%%",
				 BMS_AnalysisData.SOC * 100.0f,
				 BMS_AnalysisData.CapacityLearned,
				 BMS_AnalysisData.SOH);
	}
	else
	{
		if (SocRestorePending == true)
		{
			BMS_WARNING("SOC restore rejected saved=%.1f%% ocv=%.1f%%",
						SocRestoreData.Soc * 100.0f,
						ocv_soc * 100.0f);
		}
		BMS_AnalysisData.SOC = ocv_soc;
		BMS_AnalysisData.CapacityLearned = BMS_AnalysisData.CapacityRated;
		BMS_AnalysisUpdateSoh();
		BMS_AnalysisTempCal();
	}

	SocRestorePending = false;
	BMS_AnalysisSyncCapacityFromSoc();
	LastAhTick = osKernelGetTickCount();
	BMS_AnalysisResetRestDetection();
	SocInitialized = true;

	BMS_INFO("SOC ready %.1f%% ocv=%.1f%% min=%.3fV",
			 BMS_AnalysisData.SOC * 100.0f,
			 ocv_soc * 100.0f,
			 BMS_MonitorData.CellData[0].CellVoltage);
	return true;
}
/**
 * @brief 计算当前最低单体电压对应的SOC比例。
 * @param 无。
 * @return 返回 0.0～1.0 范围内的 OCV-SOC 比例。
 */
static float BMS_AnalysisOcvSocValue(void)
{
	return BMS_AnalysisOcvToSoc((uint16_t)(BMS_MonitorData.CellData[0].CellVoltage * 1000.0f)) / 1000.0f;
}

/**
 * @brief 清空本轮静置判定状态，但保留跨工况的容量学习锚点。
 * @param 无。
 * @return 无返回值。
 */
static void BMS_AnalysisResetRestDetection(void)
{
	SocRestStartTick = 0U;
	LastOcvCorrectTick = 0U;
	SocRestVoltageWindowTick = 0U;
	SocRestVoltageReference = 0.0f;
	SocRestVoltageStable = false;
	SocRestAnchorHandled = false;
}

/**
 * @brief 根据学习容量与额定容量计算SOH并限制在0～100%。
 * @param 无。
 * @return 无返回值。
 */
static void BMS_AnalysisUpdateSoh(void)
{
	float soh;

	if (BMS_AnalysisData.CapacityRated <= 0.0f)
	{
		BMS_AnalysisData.SOH = 0U;
		return;
	}

	soh = BMS_AnalysisData.CapacityLearned * 100.0f /
		  BMS_AnalysisData.CapacityRated;
	soh = BMS_AnalysisLimit(soh, 0.0f, 100.0f);
	BMS_AnalysisData.SOH = (uint8_t)(soh + 0.5f);
}

/**
 * @brief 使用两次可信静置OCV锚点间的净安时量学习25摄氏度基准容量。
 * @param ocv_soc 当前静置最低单体对应的OCV-SOC，范围0.0～1.0。
 * @return 无返回值。
 */
static void BMS_AnalysisCapacityLearnAtRest(float ocv_soc)
{
#if (SOC_EST_CAPACITY_LEARN_ENABLE != 0)
	float soc_span;
	float net_ah;
	float throughput_ah;
	float net_ratio;
	float estimated_capacity;
	float min_capacity;
	float max_capacity;

	if (CapacityLearnAnchorValid == false)
	{
		CapacityLearnAnchorSoc = ocv_soc;
		CapacityLearnAnchorNetAh = SocCoulombNetAh;
		CapacityLearnAnchorThroughputAh = SocCoulombThroughputAh;
		CapacityLearnAnchorValid = true;
		BMS_INFO("Capacity learn anchor SOC=%.1f%%", ocv_soc * 100.0f);
		return;
	}

	soc_span = BMS_AnalysisAbs(ocv_soc - CapacityLearnAnchorSoc);
	net_ah = BMS_AnalysisAbs(SocCoulombNetAh - CapacityLearnAnchorNetAh);
	throughput_ah = SocCoulombThroughputAh - CapacityLearnAnchorThroughputAh;

	if (soc_span < SOC_EST_CAPACITY_LEARN_MIN_SOC_SPAN || throughput_ah <= 0.0f)
	{
		return;
	}

	net_ratio = net_ah / throughput_ah;
	estimated_capacity = net_ah / soc_span;
	min_capacity = BMS_AnalysisData.CapacityRated * SOC_EST_CAPACITY_MIN_RATIO;
	max_capacity = BMS_AnalysisData.CapacityRated * SOC_EST_CAPACITY_MAX_RATIO;

	if (net_ratio >= SOC_EST_CAPACITY_LEARN_MIN_NET_RATIO &&
		estimated_capacity >= min_capacity &&
		estimated_capacity <= max_capacity)
	{
		BMS_AnalysisData.CapacityLearned +=
			(estimated_capacity - BMS_AnalysisData.CapacityLearned) *
			SOC_EST_CAPACITY_LEARN_FILTER_GAIN;
		BMS_AnalysisData.CapacityLearned =
			BMS_AnalysisLimit(BMS_AnalysisData.CapacityLearned, min_capacity, max_capacity);
		BMS_AnalysisUpdateSoh();
		BMS_AnalysisTempCal();
		BMS_AnalysisSyncCapacityFromSoc();
		BMS_INFO("Capacity learned sample=%.3fAh filtered=%.3fAh SOH=%u%%",
				 estimated_capacity,
				 BMS_AnalysisData.CapacityLearned,
				 BMS_AnalysisData.SOH);
	}
	else
	{
		BMS_WARNING("Capacity sample rejected span=%.1f%% net=%.2f cap=%.3fAh",
					soc_span * 100.0f,
					net_ratio,
					estimated_capacity);
	}

	CapacityLearnAnchorSoc = ocv_soc;
	CapacityLearnAnchorNetAh = SocCoulombNetAh;
	CapacityLearnAnchorThroughputAh = SocCoulombThroughputAh;
#else
	(void)ocv_soc;
#endif
}

/**
 * @brief 按变化量、最小间隔和休眠边沿决定是否双页保存SOC状态。
 * @param 无。
 * @return 无返回值。
 */
static void BMS_AnalysisPersistMaybe(void)
{
#if (SOC_EST_PERSIST_ENABLE != 0)
	BMS_SocPersistDataTypedef data;
	uint32_t now;
	uint32_t elapsed;
	bool entering_sleep;
	bool changed;
	bool should_save;

	if (SocInitialized == false)
	{
		return;
	}

	now = osKernelGetTickCount();
	elapsed = (uint32_t)(now - LastSocPersistTick);
	entering_sleep = (BMS_GlobalParam.SysMode == BMS_MODE_SLEEP &&
					  LastPersistObservedMode != BMS_MODE_SLEEP);
	LastPersistObservedMode = BMS_GlobalParam.SysMode;

	if (SocPersistBaselineValid == false)
	{
		changed = true;
		should_save = (elapsed >= SOC_EST_PERSIST_FIRST_SAVE_MS);
	}
	else
	{
		changed = (BMS_AnalysisAbs(BMS_AnalysisData.SOC - LastPersistedSoc) >=
				   SOC_EST_PERSIST_SOC_DELTA) ||
			  (BMS_AnalysisAbs(BMS_AnalysisData.CapacityLearned -
							LastPersistedCapacityAh) >=
				   BMS_AnalysisData.CapacityRated * 0.01f) ||
			  (BMS_AnalysisData.LoopCount != LastPersistedLoopCount);

		should_save = ((changed == true) &&
					   (elapsed >= SOC_EST_PERSIST_MIN_INTERVAL_MS || entering_sleep)) ||
					  (elapsed >= SOC_EST_PERSIST_FORCE_INTERVAL_MS);
	}

	if (should_save == false)
	{
		return;
	}

	data.Soc = BMS_AnalysisData.SOC;
	data.CapacityLearnedAh = BMS_AnalysisData.CapacityLearned;
	data.CapacityLoopAh = BMS_AnalysisData.CapacityLoop;
	data.LoopCount = BMS_AnalysisData.LoopCount;

	if (BMS_SocStoreSave(&data) == true)
	{
		SocPersistBaselineValid = true;
		LastSocPersistTick = now;
		LastPersistedSoc = data.Soc;
		LastPersistedCapacityAh = data.CapacityLearnedAh;
		LastPersistedLoopCount = data.LoopCount;
		BMS_INFO("SOC saved %.1f%% learned=%.3fAh loops=%u",
				 data.Soc * 100.0f,
				 data.CapacityLearnedAh,
				 data.LoopCount);
	}
#endif
}

/**
 * @brief 将浮点数限制在指定上下限范围内。
 * @param value 待限制的浮点数值。
 * @param min 允许的最小值。
 * @param max 允许的最大值。
 * @return 返回限制在 min～max 范围内的数值。
 */
static float BMS_AnalysisLimit(float value, float min, float max)
{
	if (value < min)
	{
		return min;
	}
	if (value > max)
	{
		return max;
	}
	return value;
}
/**
 * @brief 计算浮点数绝对值。
 * @param value 待求绝对值的浮点数。
 * @return 返回 value 的绝对值。
 */
static float BMS_AnalysisAbs(float value)
{
	return (value >= 0.0f) ? value : -value;
}

/**
 * @brief 将SOC限制在0至1范围内。
 * @param 无。
 * @return 无返回值。
 */
static void BMS_AnalysisClampSoc(void)
{
	if (BMS_AnalysisData.SOC < 0.0f) {
		BMS_AnalysisData.SOC = 0.0f;
	} else if (BMS_AnalysisData.SOC > 1.0f) {
		BMS_AnalysisData.SOC = 1.0f;
	}
}

/**
 * @brief 根据SOC同步计算剩余容量。
 * @param 无。
 * @return 无返回值。
 */
static void BMS_AnalysisSyncCapacityFromSoc(void)
{
	BMS_AnalysisClampSoc();

	if (BMS_AnalysisData.CapacityReal <= 0.0f) {
		BMS_AnalysisData.CapacityReal = BMS_AnalysisData.CapacityRated;
	}

	BMS_AnalysisData.CapacityRemain = BMS_AnalysisData.CapacityReal * BMS_AnalysisData.SOC;
}

/**
 * @brief 运行电池分析任务，周期更新统计量、容量和SOC。
 * @param paramter 任务入口参数，本函数未使用。
 * @return 无返回值。
 */
static void BMS_AnalysisTaskEntry(void *paramter)
{
	(void)paramter;

	while (1) {
		BMS_AnalysisEasy();
		Bms_HalCurrentZeroCalibrate(
			BMS_EnergyData.OutputMode == BMS_ENERGY_OUTPUT_OFF &&
			BMS_GlobalParam.Charge == BMS_STATE_DISABLE &&
			BMS_GlobalParam.Discharge == BMS_STATE_DISABLE);
		BMS_AnalysisCalCap();
		BMS_AnalysisSocCheck();
		BMS_AnalysisPersistMaybe();
		osDelay(ANALYSISI_TASK_PERIOD);
	}
}

/**
 * @brief 初始化分析数据并创建电池分析任务。
 * @param 无。
 * @return 无返回值。
 */
void BMS_AnalysisInit(void)
{
	osThreadId_t thread;

	if (BMS_AnalysisData.CapacityRated <= 0.0f) {
		BMS_AnalysisData.CapacityRated = BMS_BATTERY_CAPACITY;
	}

	BMS_AnalysisCapAndSocInit();
	if (BMS_SocStoreLoad(&SocRestoreData) == true)
	{
		SocRestorePending = true;
		BMS_INFO("SOC backup found %.1f%% learned=%.3fAh",
				 SocRestoreData.Soc * 100.0f,
				 SocRestoreData.CapacityLearnedAh);
	}

	thread = osThreadNew(BMS_AnalysisTaskEntry, NULL, &analysisTask_attributes);
	if (thread == NULL) {
		BMS_ERROR("Create Task Fail");
	}
}
