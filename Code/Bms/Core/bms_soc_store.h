#ifndef __BMS_SOC_STORE_H__
#define __BMS_SOC_STORE_H__

#include <stdbool.h>
#include <stdint.h>

typedef struct
{
	float Soc;
	float CapacityLearnedAh;
	float CapacityLoopAh;
	uint32_t LoopCount;
} BMS_SocPersistDataTypedef;

bool BMS_SocStoreLoad(BMS_SocPersistDataTypedef *data);
bool BMS_SocStoreSave(const BMS_SocPersistDataTypedef *data);

#endif
