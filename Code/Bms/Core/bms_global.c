#include "bms_global.h"




/*
 * 鍏ㄥ眬杩愯鍙傛暟锛氫繚瀛樼郴缁熸ā寮忋?佸疄闄呯數鑺?娓╁害閫氶亾鏁伴噺锛屼互鍙婂厖鐢点?佹斁鐢点?佸潎琛＄敤鎴峰紑鍏崇姸鎬併??
 */
BMS_GlobalParamTypedef BMS_GlobalParam = 
{
	.SysMode 			= BMS_MODE_STANDBY,
	.Cell_Real_Number 	= BMS_CELL_MAX,
	.Temp_Real_Number 	= BMS_TEMP_MAX,
#if ((BMS_OUTPUT_MODE == BMS_OUTPUT_MODE_CHARGE_ONLY) || \
     (BMS_OUTPUT_MODE == BMS_OUTPUT_MODE_BOTH) || \
     ((BMS_OUTPUT_MODE == BMS_OUTPUT_MODE_KEY_SWITCH) && \
      (BMS_KEY_SWITCH_DEFAULT_MODE == BMS_KEY_SWITCH_MODE_CHARGE)))
	.Charge 			= BMS_STATE_ENABLE,
#else
	.Charge 			= BMS_STATE_DISABLE,
#endif
#if ((BMS_OUTPUT_MODE == BMS_OUTPUT_MODE_DISCHARGE_ONLY) || \
     (BMS_OUTPUT_MODE == BMS_OUTPUT_MODE_BOTH) || \
     ((BMS_OUTPUT_MODE == BMS_OUTPUT_MODE_KEY_SWITCH) && \
      (BMS_KEY_SWITCH_DEFAULT_MODE == BMS_KEY_SWITCH_MODE_DISCHARGE)))
	.Discharge 		= BMS_STATE_ENABLE,
#else
	.Discharge 		= BMS_STATE_DISABLE,
#endif
#if (BMS_BALANCE_DEFAULT_ENABLE != 0U)
	.Balance		 	= BMS_STATE_ENABLE,
#else
	.Balance		 	= BMS_STATE_DISABLE,
#endif
};



