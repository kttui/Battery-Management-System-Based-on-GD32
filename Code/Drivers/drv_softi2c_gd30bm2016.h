#ifndef __DRV_SOFTI2C_GD30BM2016_H__
#define __DRV_SOFTI2C_GD30BM2016_H__

#include <stdbool.h>
#include <stdint.h>

#include "bms_config.h"

#define GD30BM2016_I2C_ADDR             0x10U

#define GD30BM2016_CELL_MAX             BMS_CELL_MAX
#define GD30BM2016_TEMP_MAX             BMS_TEMP_MAX

/* Board shunt: three 4 mOhm resistors in parallel. */
#define GD30BM2016_SHUNT_RESISTOR_MOHM  (4.0f / 3.0f)
/* CC_GAIN = 30 / Rsense(ohm) / userA, so 0x0AFE with 1.333 mOhm is about 8 mA/LSB. */
#define GD30BM2016_CURRENT_USER_MA      8.0f
#define GD30BM2016_STACK_USER_MV        8.0f
#define GD30BM2016_CC_GAIN              0x0AFEU

#define GD30BM2016_DEBUG_LEVEL          1

#if (GD30BM2016_DEBUG_LEVEL == 0)
#define GD30BM2016_ERROR(...)           do {} while (0)
#define GD30BM2016_WARNING(...)         do {} while (0)
#define GD30BM2016_INFO(...)            do {} while (0)
#else
#define GD30BM2016_ERROR(...)                                   \
    do {                                                        \
        GD30BM2016_Log("[GD30BM2016 ERROR]", __VA_ARGS__);      \
    } while (0)
#define GD30BM2016_WARNING(...)                                 \
    do {                                                        \
        GD30BM2016_Log("[GD30BM2016 WARNING]", __VA_ARGS__);    \
    } while (0)
#define GD30BM2016_INFO(...)                                    \
    do {                                                        \
        GD30BM2016_Log("[GD30BM2016 INFO]", __VA_ARGS__);       \
    } while (0)
#endif

/* Direct commands. */
#define GD30_CMD_CONTROL_STATUS        0x00U
#define GD30_CMD_SAFETY_STATUS_A        0x03U
#define GD30_CMD_SAFETY_STATUS_B        0x05U
#define GD30_CMD_SAFETY_STATUS_C        0x07U
#define GD30_CMD_PF_STATUS_A            0x0BU
#define GD30_CMD_PF_STATUS_B            0x0DU
#define GD30_CMD_PF_STATUS_C            0x0FU
#define GD30_CMD_BATTERY_STATUS         0x12U
#define GD30_CMD_CELL1_VOLTAGE          0x14U
#define GD30_CMD_CELL2_VOLTAGE          0x16U
#define GD30_CMD_CELL3_VOLTAGE          0x18U
#define GD30_CMD_CELL4_VOLTAGE          0x1AU
#define GD30_CMD_CELL16_VOLTAGE         0x3CU
#define GD30_CMD_STACK_VOLTAGE          0x34U
#define GD30_CMD_PACK_VOLTAGE           0x36U
#define GD30_CMD_CC2_CURRENT            0x3AU
#define GD30_CMD_ALARM_STATUS           0x62U
#define GD30_CMD_ALARM_RAW_STATUS       0x64U
#define GD30_CMD_TS1_TEMPERATURE        0x70U
#define GD30_CMD_TS2_TEMPERATURE        0x72U
#define GD30_CMD_TS3_TEMPERATURE        0x74U
#define GD30_CMD_FET_STATUS             0x7FU

/* Command-only subcommands. */
#define GD30_SUBCMD_DEEPSLEEP           0x0304U
#define GD30_SUBCMD_SHUTDOWN            0x0308U
#define GD30_SUBCMD_CHIP_RESET          0x03CCU
#define GD30_SUBCMD_FET_ENABLE          0x0324U
#define GD30_SUBCMD_SEAL                0x032CU
#define GD30_SUBCMD_SET_CFGUPDATE       0x0338U
#define GD30_SUBCMD_EXIT_CFGUPDATE      0x033CU
#define GD30_SUBCMD_ALL_FETS_OFF        0x0348U
#define GD30_SUBCMD_ALL_FETS_ON         0x034CU
#define GD30_SUBCMD_SLEEP_ENABLE        0x0350U
#define GD30_SUBCMD_SLEEP_DISABLE       0x0354U

/* Subcommands with data / RAM registers. */
#define GD30_SUBCMD_UNSEAL_KEY_STEP1   0x0020U
#define GD30_SUBCMD_UNSEAL_KEY_STEP2   0x0022U
#define GD30_SUBCMD_FULL_KEY_STEP1     0x0024U
#define GD30_SUBCMD_FULL_KEY_STEP2     0x0026U
#define GD30_SUBCMD_FET_CONTROL         0x0250U
#define GD30_SUBCMD_ACCESS_KEY_STEP1    0x0280U
#define GD30_SUBCMD_ACCESS_KEY_STEP2    0x0282U
#define GD30_SUBCMD_MANUFACTURING_STATUS 0x0030U
#define GD30_REG_POWER_CONFIG           0x8000U
#define GD30_REG_REG12_CONFIG           0x8002U
#define GD30_REG_REG0_CONFIG            0x8003U
#define GD30_REG_COMM_IDLE_TIME         0x8008U
#define GD30_REG_MANUFACTURING          0x8013U
#define GD30_REG_SECURITY_SETTINGS      0x8048U
#define GD30_REG_ADC_CLK_CFG            0x8049U
#define GD30_REG_TS1_CONFIG             0x8209U
#define GD30_REG_TS23_CONFIG            0x820AU
#define GD30_REG_PAD_CFG                0x8210U

#define GD30_TS_OPT_THERM_CELL          0x05U
#define GD30_TS1_CONFIG_THERM_CELL      (GD30_TS_OPT_THERM_CELL << 4)
#define GD30_TS23_CONFIG_DISABLE        0x00U
#define GD30_PAD_CFG_TS_PULLUP_18K      0x90U
#define GD30_ENABLED_PROTECTIONS_B_INTERNAL_TEMP 0x44U
#define GD30_REG_CC_GAIN                0x842CU
#define GD30_REG_COEFF_A1_18K           0x8448U
#define GD30_REG_COEFF_A2_18K           0x844AU
#define GD30_REG_COEFF_A3_18K           0x844CU
#define GD30_REG_COEFF_A4_18K           0x844EU
#define GD30_REG_ADC_EN                 0x846AU
#define GD30_REG_DA_CONFIG              0x8807U
#define GD30_REG_VCELL_MODE             0x8808U
#define GD30_REG_PROTECTION_CONFIG      0x880AU
#define GD30_REG_ENABLED_PROTECTIONS_A  0x880CU
#define GD30_REG_ENABLED_PROTECTIONS_B  0x880DU
#define GD30_REG_ENABLED_PROTECTIONS_C  0x880EU
#define GD30_REG_FET_OPTIONS            0x8824U
#define GD30_REG_CHG_PUMP_CONTROL       0x8825U
#define GD30_REG_CUV_THRESHOLD          0x882CU
#define GD30_REG_CUV_DELAY              0x882EU
#define GD30_REG_COV_THRESHOLD          0x8832U
#define GD30_REG_COV_DELAY              0x8834U
#define GD30_REG_OCC_THRESHOLD          0x883AU
#define GD30_REG_OCC_DELAY              0x883CU
#define GD30_REG_OCD1_THRESHOLD         0x8844U
#define GD30_REG_OCD1_DELAY             0x8846U
#define GD30_REG_SCD_THRESHOLD          0x884BU
#define GD30_REG_SCD_DELAY              0x884CU
#define GD30_REG_BALANCE_CONFIG         0x8C00U
#define GD30_REG_CELL_BALANCE_INTERVAL  0x8C04U
#define GD30_REG_CELL_BALANCE_MAX_CELLS 0x8C05U

#define GD30_SUBCMD_CB_ACTIVE_CELLS     0x0200U

#define GD30_VCELL_MODE_5S_VC0_TO_VC4_VC16  0x8007U
#define GD30_CONTROL_STATUS_AUTO_LOAD_TIMEOUT_BIT (1U << 3)
#define GD30_CONTROL_STATUS_DEEPSLEEP_BIT         (1U << 2)
#define GD30_BATTERY_STATUS_CFGUPDATE_BIT   (1U << 0)
#define GD30_BATTERY_STATUS_SLEEP_EN_BIT    (1U << 2)
#define GD30_BATTERY_STATUS_SLEEP_BIT       (1U << 15)
#define GD30_BATTERY_STATUS_SEC_MASK        (3U << 8)

#define GD30_FET_CONTROL_DSG_OFF        (1U << 0)
#define GD30_FET_CONTROL_PDSG_OFF       (1U << 1)
#define GD30_FET_CONTROL_CHG_OFF        (1U << 2)
#define GD30_FET_CONTROL_PCHG_OFF       (1U << 3)

#define GD30_SAFETY_A_SCD_BIT           (1U << 7)
#define GD30_SAFETY_A_OCD2_BIT          (1U << 6)
#define GD30_SAFETY_A_OCD1_BIT          (1U << 5)
#define GD30_SAFETY_A_OCC_BIT           (1U << 4)
#define GD30_SAFETY_A_COV_BIT           (1U << 3)
#define GD30_SAFETY_A_CUV_BIT           (1U << 2)
#define GD30_SAFETY_C_OCD3_BIT          (1U << 7)
#define GD30_SAFETY_C_SCDL_BIT          (1U << 6)
#define GD30_SAFETY_C_OCDL_BIT          (1U << 5)
#define GD30_SAFETY_C_COVL_BIT          (1U << 4)

typedef enum {
    GD30_STATE_ENABLE,
    GD30_STATE_DISABLE
} GD30BM2016_StateTypedef;

typedef enum {
    GD30_CHG_CONTROL = 0x01,
    GD30_DSG_CONTROL = 0x02,
    GD30_PCHG_CONTROL = 0x04,
    GD30_PDSG_CONTROL = 0x08
} GD30BM2016_ControlTypedef;

typedef enum {
    GD30_CELL_INDEX1  = 0x0001,
    GD30_CELL_INDEX2  = 0x0002,
    GD30_CELL_INDEX3  = 0x0004,
    GD30_CELL_INDEX4  = 0x0008,
    GD30_CELL_INDEX5  = 0x0010,
    GD30_CELL_ALL     = 0x001F,
} GD30BM2016_CellIndexTypedef;

typedef enum {
    GD30_SCD_DELAY_50us  = 0x00,
    GD30_SCD_DELAY_100us = 0x01,
    GD30_SCD_DELAY_200us = 0x02,
    GD30_SCD_DELAY_400us = 0x03,
} GD30BM2016_SCDDelayTypedef;

typedef enum {
    GD30_OCD_DELAY_10ms   = 0x00,
    GD30_OCD_DELAY_20ms   = 0x01,
    GD30_OCD_DELAY_40ms   = 0x02,
    GD30_OCD_DELAY_80ms   = 0x03,
    GD30_OCD_DELAY_160ms  = 0x04,
    GD30_OCD_DELAY_320ms  = 0x05,
    GD30_OCD_DELAY_640ms  = 0x06,
    GD30_OCD_DELAY_1280ms = 0x07,
} GD30BM2016_OCDDelayTypedef;

typedef enum {
    GD30_OV_DELAY_1s = 0x00,
    GD30_OV_DELAY_2s = 0x01,
    GD30_OV_DELAY_4s = 0x02,
    GD30_OV_DELAY_8s = 0x03,
} GD30BM2016_OVDelayTypedef;

typedef enum {
    GD30_UV_DELAY_1s  = 0x00,
    GD30_UV_DELAY_4s  = 0x01,
    GD30_UV_DELAY_8s  = 0x02,
    GD30_UV_DELAY_16s = 0x03,
} GD30BM2016_UVDelayTypedef;

typedef struct {
    GD30BM2016_SCDDelayTypedef SCDDelay;
    GD30BM2016_OCDDelayTypedef OCDDelay;
    GD30BM2016_UVDelayTypedef UVDelay;
    GD30BM2016_OVDelayTypedef OVDelay;
    uint16_t UVPThreshold;
    uint16_t OVPThreshold;
    float OCCThreshold;
    float OCDThreshold;
} GD30BM2016_ConfigDataTypedef;

typedef struct {
    void (*ocd)(void);
    void (*scd)(void);
    void (*ov)(void);
    void (*uv)(void);
    void (*ovrd)(void);
    void (*device)(void);
    void (*cc)(void);
} GD30BM2016_AlertOpsTypedf;

typedef struct {
    GD30BM2016_AlertOpsTypedf AlertOps;
    GD30BM2016_ConfigDataTypedef ConfigData;
} GD30BM2016_InitDataTypedef;

typedef struct {
    float CellVoltage[GD30BM2016_CELL_MAX];
    float TsxTemperature[GD30BM2016_TEMP_MAX];
    float BatteryCurrent;
    float BatteryVoltage;
    float DieTemperature;
} GD30BM2016_SampleDataTypedef;

extern GD30BM2016_SampleDataTypedef GD30BM2016_SampleData;

void GD30BM2016_Log(const char *tag, const char *format, ...);
bool GD30BM2016_Initialize(GD30BM2016_InitDataTypedef *InitData);
void GD30BM2016_EXTI_Callback(uint32_t GPIO_Pin);
void GD30BM2016_Wakeup(void);
void GD30BM2016_EntryShip(void);
bool GD30BM2016_LoadDetect(void);
void GD30BM2016_ControlDSGOrCHG(GD30BM2016_ControlTypedef ControlType, GD30BM2016_StateTypedef NewState);
void GD30BM2016_CellBalanceControl(GD30BM2016_CellIndexTypedef CellIndex, GD30BM2016_StateTypedef NewState);

void GD30BM2016_UpdateCellVolt(void);
void GD30BM2016_UpdateTsTemp(void);
void GD30BM2016_UpdateDieTemp(void);
void GD30BM2016_UpdateCurrent(void);
void GD30BM2016_UpdateBatVolt(void);

void GD30BM2016_SCDDelaySet(GD30BM2016_SCDDelayTypedef SCDDelay);
void GD30BM2016_OCDDelaySet(GD30BM2016_OCDDelayTypedef OCDDelay);
void GD30BM2016_UVDelaySet(GD30BM2016_UVDelayTypedef UVDelay);
void GD30BM2016_OVDelaySet(GD30BM2016_OVDelayTypedef OVDelay);
void GD30BM2016_UVPThresholdSet(uint16_t UVPThreshold);
void GD30BM2016_OVPThresholdSet(uint16_t OVPThreshold);

#endif
