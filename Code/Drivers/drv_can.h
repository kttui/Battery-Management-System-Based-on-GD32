#ifndef __DRV_CAN_H__
#define __DRV_CAN_H__

#include <stdbool.h>
#include <stdint.h>

typedef enum
{
	CAN_ENABLE_PWR = 0u,
	CAN_DISABLE_PWR,
}CAN_PwrStateTypedef;

typedef struct
{
	uint32_t IdType;
	uint32_t StdId;
	uint32_t ExtId;
	uint8_t Data[8];
	uint8_t Length;
	uint8_t IsDataFrame;
	uint8_t IsClassicFrame;
}CAN_RxFrameTypedef;

typedef struct
{
	uint8_t Mailbox;
	uint8_t State;
	uint8_t Error;
	uint8_t TxErrorCount;
	uint8_t RxErrorCount;
	uint8_t BusOff;
	uint8_t PassiveError;
	uint8_t WarningError;
}CAN_TxStatusTypedef;



void CAN_Initialize(void);
void CAN_SendData(uint32_t IdType,uint32_t StdId,uint32_t ExtId,uint8_t *pData,uint32_t length);
void CAN_SendDataWithStatus(uint32_t IdType,uint32_t StdId,uint32_t ExtId,uint8_t *pData,uint32_t length,CAN_TxStatusTypedef *pStatus);
bool CAN_ReceiveData(CAN_RxFrameTypedef *pFrame);
void CAN_ConfigStdFilter(uint16_t filter_number, uint32_t std_id, uint32_t mask);
void CAN_PwrState(CAN_PwrStateTypedef NewState);

void CAN_SendTest(void);


#endif
