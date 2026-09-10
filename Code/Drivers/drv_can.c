#include "drv_can.h"

#include <string.h>

#include "can.h"
#include "main.h"

/**
 * @brief 使能CAN收发器电源。
 * @param 无。
 * @return 无返回值。
 */
void CAN_Initialize(void)
{
    CAN_PwrState(CAN_ENABLE_PWR);
}

/**
 * @brief 发送一帧Classic CAN数据。
 * @param IdType CAN标识符类型。
 * @param StdId 标准帧ID。
 * @param ExtId 扩展帧ID。
 * @param pData 待发送的 CAN 数据缓冲区，最多使用前 8 字节。
 * @param length 数据长度。
 * @return 无返回值。
 */
void CAN_SendData(uint32_t IdType, uint32_t StdId, uint32_t ExtId, uint8_t *pData, uint32_t length)
{
    CAN_SendDataWithStatus(IdType, StdId, ExtId, pData, length, NULL);
}

/**
 * @brief 发送CAN数据并返回邮箱及控制器错误状态。
 * @param IdType CAN标识符类型。
 * @param StdId 标准帧ID。
 * @param ExtId 扩展帧ID。
 * @param pData 待发送的 CAN 数据缓冲区，最多使用前 8 字节。
 * @param length 数据长度。
 * @param pStatus CAN发送状态输出对象。
 * @return 无返回值。
 */
void CAN_SendDataWithStatus(uint32_t IdType, uint32_t StdId, uint32_t ExtId, uint8_t *pData, uint32_t length, CAN_TxStatusTypedef *pStatus)
{
    can_trasnmit_message_struct tx_message;
    uint8_t mailbox;

    if (length > 8U) {
        length = 8U;
    }

    can_struct_para_init(CAN_TX_MESSAGE_STRUCT, &tx_message);
    tx_message.tx_ff = (IdType == CAN_ID_EXT) ? CAN_FF_EXTENDED : CAN_FF_STANDARD;
    tx_message.tx_ft = CAN_FT_DATA;
    tx_message.tx_sfid = StdId & 0x7FFU;
    tx_message.tx_efid = ExtId & 0x1FFFFFFFU;
    tx_message.tx_dlen = (uint8_t)length;

    if ((pData != NULL) && (length > 0U)) {
        memcpy(tx_message.tx_data, pData, length);
    }

    mailbox = can_message_transmit(BMS_CAN_PERIPH, &tx_message);

    if (pStatus != NULL) {
        pStatus->Mailbox = mailbox;
        pStatus->State = (mailbox == CAN_NOMAILBOX) ? CAN_TRANSMIT_NOMAILBOX :
                         (uint8_t)can_transmit_states(BMS_CAN_PERIPH, mailbox);
        pStatus->Error = (uint8_t)can_error_get(BMS_CAN_PERIPH);
        pStatus->TxErrorCount = can_transmit_error_number_get(BMS_CAN_PERIPH);
        pStatus->RxErrorCount = can_receive_error_number_get(BMS_CAN_PERIPH);
        pStatus->BusOff = (can_flag_get(BMS_CAN_PERIPH, CAN_FLAG_BOERR) == SET) ? 1U : 0U;
        pStatus->PassiveError = (can_flag_get(BMS_CAN_PERIPH, CAN_FLAG_PERR) == SET) ? 1U : 0U;
        pStatus->WarningError = (can_flag_get(BMS_CAN_PERIPH, CAN_FLAG_WERR) == SET) ? 1U : 0U;
    }
}

/**
 * @brief 从CAN FIFO读取并转换一帧数据。
 * @param pFrame CAN接收帧输出对象。
 * @return true表示操作成功，false表示操作失败。
 */
bool CAN_ReceiveData(CAN_RxFrameTypedef *pFrame)
{
    can_receive_message_struct rx_message;
    uint8_t fifo_number;
    uint8_t copy_length;

    if (pFrame == NULL) {
        return false;
    }

    if (can_receive_message_length_get(BMS_CAN_PERIPH, CAN_FIFO0) > 0U) {
        fifo_number = CAN_FIFO0;
    } else if (can_receive_message_length_get(BMS_CAN_PERIPH, CAN_FIFO1) > 0U) {
        fifo_number = CAN_FIFO1;
    } else {
        return false;
    }

    can_struct_para_init(CAN_RX_MESSAGE_STRUCT, &rx_message);
    can_message_receive(BMS_CAN_PERIPH, fifo_number, &rx_message);

    memset(pFrame, 0, sizeof(*pFrame));
    pFrame->IdType = (rx_message.rx_ff == CAN_FF_EXTENDED) ? CAN_ID_EXT : CAN_ID_STD;
    pFrame->StdId = rx_message.rx_sfid & 0x7FFU;
    pFrame->ExtId = rx_message.rx_efid & 0x1FFFFFFFU;
    pFrame->Length = rx_message.rx_dlen;
    pFrame->IsDataFrame = (rx_message.rx_ft == CAN_FT_DATA) ? 1U : 0U;
    pFrame->IsClassicFrame = (rx_message.fd_flag == CAN_FDF_CLASSIC) ? 1U : 0U;

    copy_length = (rx_message.rx_dlen > 8U) ? 8U : rx_message.rx_dlen;
    if (copy_length > 0U) {
        memcpy(pFrame->Data, rx_message.rx_data, copy_length);
    }

    return true;
}

/**
 * @brief 配置一个标准帧掩码过滤器。
 * @param filter_number CAN 硬件过滤器编号。
 * @param std_id 要接收的 11 位标准帧 ID。
 * @param mask 标准帧 ID 的 11 位匹配掩码。
 * @return 无返回值。
 */
void CAN_ConfigStdFilter(uint16_t filter_number, uint32_t std_id, uint32_t mask)
{
    can_filter_mask_mode_init(std_id & 0x7FFU,
                              mask & 0x7FFU,
                              CAN_STANDARD_FIFO0,
                              filter_number);
}

/**
 * @brief 控制CAN收发器电源状态。
 * @param NewState 目标使能状态。
 * @return 无返回值。
 */
void CAN_PwrState(CAN_PwrStateTypedef NewState)
{
    gpio_bit_write(CAN_PWREN_GPIO_Port, CAN_PWREN_Pin,
                   (NewState == CAN_ENABLE_PWR) ? RESET : SET);
}

/**
 * @brief 生成递增测试数据并交替发送标准帧和扩展帧。
 * @param 无。
 * @return 无返回值。
 */
void CAN_SendTest(void)
{
    static uint8_t send_data[8] = {0, 1, 2, 3, 4, 5, 6, 7};
    static uint8_t id_type_std = 0;

    for (uint32_t i = 0U; i < sizeof(send_data); i++) {
        send_data[i]++;
    }

    if (id_type_std == 1U) {
        CAN_SendData(CAN_ID_STD, 0x123U, 0x321U, send_data, 8U);
        id_type_std = 0U;
    } else {
        CAN_SendData(CAN_ID_EXT, 0x123U, 0x321U, send_data, 8U);
        id_type_std = 1U;
    }
}
