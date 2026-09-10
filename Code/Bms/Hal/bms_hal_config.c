#include "bms_hal_config.h"
#include "bms_config.h"

#include "drv_softi2c_gd30bm2016.h"








/*
 * 函数作用：设置短路放电保护 SCD 的硬件触发延时时间。
 */
/**
 * @brief 设置短路放电保护 SCD 的硬件触发延时时间。
 * @param SCDDelay 短路保护延时枚举值。
 * @return 无返回值。
 */
void BMS_HalCfgSCDDelaySet(BMS_SCDDelayTypedef SCDDelay)
{
	GD30BM2016_SCDDelaySet((GD30BM2016_SCDDelayTypedef)SCDDelay);
}

/*
 * 函数作用：设置放电过流保护 OCD 的硬件触发延时时间。
 */
/**
 * @brief 设置放电过流保护 OCD 的硬件触发延时时间。
 * @param OCDDelay 放电过流保护延时枚举值。
 * @return 无返回值。
 */
void BMS_HalCfgOCDDelaySet(BMS_OCDDelayTypedef OCDDelay)
{
	GD30BM2016_OCDDelaySet((GD30BM2016_OCDDelayTypedef)OCDDelay);
}

/*
 * 函数作用：设置单体欠压保护 UV 的硬件触发延时时间。
 */
/**
 * @brief 设置单体欠压保护 UV 的硬件触发延时时间。
 * @param UVDelay 单体欠压保护延时枚举值。
 * @return 无返回值。
 */
void BMS_HalCfgUVDelaySet(BMS_UVDelayTypedef UVDelay)
{
	GD30BM2016_UVDelaySet((GD30BM2016_UVDelayTypedef)UVDelay);
}

/*
 * 函数作用：设置单体过压保护 OV 的硬件触发延时时间。
 */
/**
 * @brief 设置单体过压保护 OV 的硬件触发延时时间。
 * @param OVDelay 单体过压保护延时枚举值。
 * @return 无返回值。
 */
void BMS_HalCfgOVDelaySet(BMS_OVDelayTypedef OVDelay)
{
	GD30BM2016_OVDelaySet((GD30BM2016_OVDelayTypedef)OVDelay);
}

/*
 * 函数作用：设置单体欠压保护 UVP 的硬件阈值。
 */
/**
 * @brief 设置单体欠压保护 UVP 的硬件触发阈值。
 * @param UVPThreshold 单体欠压保护阈值，单位为毫伏。
 * @return 无返回值。
 */
void BMS_HalCfgUVPThresholdSet(uint16_t UVPThreshold)
{
	GD30BM2016_UVPThresholdSet(UVPThreshold);
}

/*
 * 函数作用：设置单体过压保护 OVP 的硬件阈值。
 */
/**
 * @brief 设置单体过压保护 OVP 的硬件触发阈值。
 * @param OVPThreshold 单体过压保护阈值，单位为毫伏。
 * @return 无返回值。
 */
void BMS_HalCfgOVPThresholdSet(uint16_t OVPThreshold)
{
	GD30BM2016_OVPThresholdSet(OVPThreshold);
}
