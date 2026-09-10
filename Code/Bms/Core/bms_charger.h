#ifndef __BMS_CHARGER_H__
#define __BMS_CHARGER_H__

#include <stdbool.h>
#include <stdint.h>

typedef enum
{
	BMS_CHARGER_STATE_INIT = 0,
	BMS_CHARGER_STATE_READY,
	BMS_CHARGER_STATE_CHARGING,
	BMS_CHARGER_STATE_FAULT,
	BMS_CHARGER_STATE_STOP,
} BMS_ChargerStateTypedef;

typedef enum
{
	BMS_CHARGER_STATUS_DISABLED = 0,
	BMS_CHARGER_STATUS_READY = 1,
	BMS_CHARGER_STATUS_CHARGING = 2,
	BMS_CHARGER_STATUS_FAULT = 3,
	BMS_CHARGER_STATUS_WAIT_BMS = 4,
} BMS_ChargerStatusTypedef;

typedef enum
{
	BMS_CHARGER_POWER_DISABLED = 0,
	BMS_CHARGER_POWER_BUCK = 1,
	BMS_CHARGER_POWER_BOOST = 2,
	BMS_CHARGER_POWER_TRANSITION = 3,
} BMS_ChargerPowerModeTypedef;

typedef enum
{
	BMS_CHARGER_FAULT_VIN_UV = 0x0001U,
	BMS_CHARGER_FAULT_VIN_OV = 0x0002U,
	BMS_CHARGER_FAULT_VOUT_OV = 0x0004U,
	BMS_CHARGER_FAULT_INPUT_OC = 0x0008U,
	BMS_CHARGER_FAULT_OUTPUT_OC = 0x0010U,
	BMS_CHARGER_FAULT_OT = 0x0020U,
	BMS_CHARGER_FAULT_ADC = 0x0040U,
	BMS_CHARGER_FAULT_COMM_TIMEOUT = 0x0080U,
	BMS_CHARGER_FAULT_PWM_SAT = 0x0100U,
	BMS_CHARGER_FAULT_BMS_TIMEOUT = 0x0200U,
	BMS_CHARGER_FAULT_BMS_ESTOP = 0x0400U,
	BMS_CHARGER_FAULT_BMS_PROTOCOL = 0x0800U,
	BMS_CHARGER_FAULT_MODE_FORBIDDEN = 0x1000U,
} BMS_ChargerFaultTypedef;

typedef struct
{
	uint32_t VinMv;
	uint32_t IinMa;
	uint32_t VoutMv;
	uint32_t IoutMa;
	uint32_t LastRxTick;
	bool Valid;
} BMS_ChargerTelemetryTypedef;

typedef struct
{
	uint16_t LatchedFault;
	uint16_t PresentFault;
	uint8_t ChargerStatus;
	int16_t TemperatureDegC;
	uint8_t PowerMode;
	bool OutputActive;
	bool BmsOnline;
	uint8_t BuckDuty10Pct;
	uint8_t BoostDuty10Pct;
	uint32_t LastRxTick;
	bool Valid;
} BMS_ChargerStatusDataTypedef;

typedef struct
{
	BMS_ChargerStateTypedef State;
	BMS_ChargerTelemetryTypedef Telemetry;
	BMS_ChargerStatusDataTypedef Status;
	uint32_t TargetVoltageMv;
	uint32_t CurrentLimitMa;
	uint8_t AliveCounter;
	bool ChargeEnableRequest;
	bool AllowBoost;
	bool AllowBuck;
	bool FaultClearPending;
	bool EmergencyStop;
	bool FaultStop;
} BMS_ChargerDataTypedef;

extern BMS_ChargerDataTypedef BMS_ChargerData;

void BMS_ChargerInit(void);
void BMS_ChargerSetTarget(uint32_t target_mv, uint32_t current_ma);
void BMS_ChargerSetChargeEnable(bool enable);
void BMS_ChargerClearFault(void);
void BMS_ChargerEmergencyStop(void);
void bms_charger_buzzer_test_set(uint8_t on);

#endif
