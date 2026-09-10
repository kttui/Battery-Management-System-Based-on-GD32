#ifndef __CAN_H__
#define __CAN_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"

#define BMS_CAN_PERIPH CAN0
#define BMS_CAN_TX_MAILBOX 0U

#define CAN_ID_STD 0U
#define CAN_ID_EXT 1U

void MX_CAN_Init(void);

#ifdef __cplusplus
}
#endif

#endif
