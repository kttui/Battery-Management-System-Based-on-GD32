#define BMS_DBG_TAG "SocStore"

#include "bms_soc_store.h"

#include <stddef.h>

#include "bms_config.h"
#include "bms_debug.h"
#include "gd32c11x_fmc.h"

#define BMS_SOC_FLASH_MAGIC       0x534F4331U
#define BMS_SOC_FLASH_VERSION     0x00010001U
#define BMS_SOC_FLASH_COMMIT      0xC05A5AA5U
#define BMS_SOC_CRC32_POLYNOMIAL  0xEDB88320U

#if ((SOC_EST_FLASH_PAGE0_ADDRESS + SOC_EST_FLASH_PAGE_SIZE) != SOC_EST_FLASH_PAGE1_ADDRESS)
#error "SOC flash pages must be contiguous"
#endif

#if ((SOC_EST_FLASH_PAGE1_ADDRESS + SOC_EST_FLASH_PAGE_SIZE) != 0x08020000U)
#error "SOC flash pages must end at the 128KB flash boundary"
#endif

typedef struct
{
	uint32_t Magic;
	uint32_t Version;
	uint32_t Sequence;
	float Soc;
	float CapacityLearnedAh;
	float CapacityLoopAh;
	uint32_t LoopCount;
	uint32_t Crc32;
	uint32_t Commit;
} BMS_SocFlashRecordTypedef;

typedef union
{
	BMS_SocFlashRecordTypedef Record;
	uint32_t Words[sizeof(BMS_SocFlashRecordTypedef) / sizeof(uint32_t)];
} BMS_SocFlashRecordUnion;

static uint32_t BMS_SocStoreActiveAddress = 0U;
static uint32_t BMS_SocStoreSequence = 0U;

/**
 * @brief 计算SOC记录的CRC32校验值。
 * @param data 待校验数据首地址。
 * @param length 数据长度，单位为字节。
 * @return 返回CRC32校验值。
 */
static uint32_t BMS_SocStoreCrc32(const uint8_t *data, uint32_t length)
{
	uint32_t crc = 0xFFFFFFFFU;

	for (uint32_t index = 0U; index < length; index++)
	{
		crc ^= data[index];
		for (uint8_t bit = 0U; bit < 8U; bit++)
		{
			crc = (crc >> 1U) ^ ((crc & 1U) ? BMS_SOC_CRC32_POLYNOMIAL : 0U);
		}
	}

	return ~crc;
}

/**
 * @brief 从指定Flash页首地址读取一条SOC记录。
 * @param address Flash页首地址。
 * @param record 接收记录的缓冲区。
 * @return 无返回值。
 */
static void BMS_SocStoreReadRecord(uint32_t address, BMS_SocFlashRecordUnion *record)
{
	volatile const uint32_t *source = (volatile const uint32_t *)address;

	for (uint32_t index = 0U; index <
		 (sizeof(record->Words) / sizeof(record->Words[0])); index++)
	{
		record->Words[index] = source[index];
	}
}

/**
 * @brief 检查SOC Flash记录的标志、数值范围和CRC。
 * @param record 待检查记录。
 * @return 记录完整有效时返回true，否则返回false。
 */
static bool BMS_SocStoreRecordValid(const BMS_SocFlashRecordTypedef *record)
{
	uint32_t crc;

	if (record->Magic != BMS_SOC_FLASH_MAGIC ||
		record->Version != BMS_SOC_FLASH_VERSION ||
		record->Commit != BMS_SOC_FLASH_COMMIT)
	{
		return false;
	}

	if (!(record->Soc >= 0.0f && record->Soc <= 1.0f) ||
		!(record->CapacityLearnedAh > 0.0f && record->CapacityLearnedAh < 1000.0f) ||
		!(record->CapacityLoopAh >= 0.0f && record->CapacityLoopAh < 1000.0f))
	{
		return false;
	}

	crc = BMS_SocStoreCrc32((const uint8_t *)record,
							 (uint32_t)offsetof(BMS_SocFlashRecordTypedef, Crc32));
	return (crc == record->Crc32);
}

/**
 * @brief 从双页备份中加载序号最新且CRC有效的SOC记录。
 * @param data 接收恢复数据的缓冲区。
 * @return 成功找到有效记录时返回true，否则返回false。
 */
bool BMS_SocStoreLoad(BMS_SocPersistDataTypedef *data)
{
#if (SOC_EST_PERSIST_ENABLE != 0)
	BMS_SocFlashRecordUnion page0;
	BMS_SocFlashRecordUnion page1;
	const BMS_SocFlashRecordTypedef *selected = NULL;
	bool page0_valid;
	bool page1_valid;

	if (data == NULL)
	{
		return false;
	}

	BMS_SocStoreReadRecord(SOC_EST_FLASH_PAGE0_ADDRESS, &page0);
	BMS_SocStoreReadRecord(SOC_EST_FLASH_PAGE1_ADDRESS, &page1);
	page0_valid = BMS_SocStoreRecordValid(&page0.Record);
	page1_valid = BMS_SocStoreRecordValid(&page1.Record);

	if (page0_valid && page1_valid)
	{
		if ((int32_t)(page1.Record.Sequence - page0.Record.Sequence) > 0)
		{
			selected = &page1.Record;
			BMS_SocStoreActiveAddress = SOC_EST_FLASH_PAGE1_ADDRESS;
		}
		else
		{
			selected = &page0.Record;
			BMS_SocStoreActiveAddress = SOC_EST_FLASH_PAGE0_ADDRESS;
		}
	}
	else if (page0_valid)
	{
		selected = &page0.Record;
		BMS_SocStoreActiveAddress = SOC_EST_FLASH_PAGE0_ADDRESS;
	}
	else if (page1_valid)
	{
		selected = &page1.Record;
		BMS_SocStoreActiveAddress = SOC_EST_FLASH_PAGE1_ADDRESS;
	}
	else
	{
		return false;
	}

	BMS_SocStoreSequence = selected->Sequence;
	data->Soc = selected->Soc;
	data->CapacityLearnedAh = selected->CapacityLearnedAh;
	data->CapacityLoopAh = selected->CapacityLoopAh;
	data->LoopCount = selected->LoopCount;
	return true;
#else
	(void)data;
	return false;
#endif
}

/**
 * @brief 将SOC状态写入非当前页，并以最后写入提交字实现断电保护。
 * @param data 待保存的SOC、学习容量和循环数据。
 * @return 擦写及回读校验全部成功时返回true，否则返回false。
 */
bool BMS_SocStoreSave(const BMS_SocPersistDataTypedef *data)
{
#if (SOC_EST_PERSIST_ENABLE != 0)
	BMS_SocFlashRecordUnion record = {0};
	BMS_SocFlashRecordUnion verify;
	uint32_t target_address;
	uint32_t primask;
	fmc_state_enum state;

	if (data == NULL || !(data->Soc >= 0.0f && data->Soc <= 1.0f) ||
		!(data->CapacityLearnedAh > 0.0f && data->CapacityLearnedAh < 1000.0f))
	{
		return false;
	}

	target_address = (BMS_SocStoreActiveAddress == SOC_EST_FLASH_PAGE0_ADDRESS) ?
					 SOC_EST_FLASH_PAGE1_ADDRESS : SOC_EST_FLASH_PAGE0_ADDRESS;
	record.Record.Magic = BMS_SOC_FLASH_MAGIC;
	record.Record.Version = BMS_SOC_FLASH_VERSION;
	record.Record.Sequence = BMS_SocStoreSequence + 1U;
	record.Record.Soc = data->Soc;
	record.Record.CapacityLearnedAh = data->CapacityLearnedAh;
	record.Record.CapacityLoopAh = data->CapacityLoopAh;
	record.Record.LoopCount = data->LoopCount;
	record.Record.Crc32 = BMS_SocStoreCrc32((const uint8_t *)&record.Record,
										(uint32_t)offsetof(BMS_SocFlashRecordTypedef, Crc32));
	record.Record.Commit = BMS_SOC_FLASH_COMMIT;

	primask = __get_PRIMASK();
	__disable_irq();
	fmc_unlock();
	fmc_program_width_set(FMC_PROG_W_32B);
	fmc_flag_clear(FMC_FLAG_END | FMC_FLAG_PGERR | FMC_FLAG_PGAERR | FMC_FLAG_WPERR);
	state = fmc_page_erase(target_address);

	if (state == FMC_READY)
	{
		uint32_t commit_index = (uint32_t)offsetof(BMS_SocFlashRecordTypedef, Commit) /
									 sizeof(uint32_t);

		for (uint32_t index = 0U; index < commit_index; index++)
		{
			state = fmc_word_program(target_address + index * sizeof(uint32_t),
								 record.Words[index]);
			if (state != FMC_READY)
			{
				break;
			}
		}

		if (state == FMC_READY)
		{
			state = fmc_word_program(target_address + commit_index * sizeof(uint32_t),
								 record.Words[commit_index]);
		}
	}

	fmc_lock();
	fmc_dbus_reset();
	if (primask == 0U)
	{
		__enable_irq();
	}

	if (state != FMC_READY)
	{
		BMS_ERROR("SOC Flash write failed state=%d", state);
		return false;
	}

	BMS_SocStoreReadRecord(target_address, &verify);
	if (BMS_SocStoreRecordValid(&verify.Record) == false ||
		verify.Record.Sequence != record.Record.Sequence)
	{
		BMS_ERROR("SOC Flash verify failed");
		return false;
	}

	BMS_SocStoreActiveAddress = target_address;
	BMS_SocStoreSequence = record.Record.Sequence;
	return true;
#else
	(void)data;
	return false;
#endif
}
