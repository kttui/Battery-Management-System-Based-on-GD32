#include "drv_softi2c_gd30bm2016.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "drv_soft_i2c.h"
#include "main.h"
#include "cmsis_os2.h"
#include "usart.h"

#define GD30BM2016_DELAY(ms) osDelay(ms)
#define GD30_ALARM_ADSCAN_BIT          (1U << 1)
#define GD30_ALARM_FULLSCAN_BIT        (1U << 7)
#define GD30_ALARM_INITCOMP_BIT        (1U << 9)
#define GD30_FIRST_SCAN_TIMEOUT_MS     1000U
#define GD30_RST_SHUT_RESET_MS         1100U
#define GD30_TS2_WAKE_WAIT_MS          10000U
#define GD30_TS2_WAKE_POLL_MS          20U
#define GD30_INIT_WAIT_MS              3000U
#define GD30_SECURITY_WAIT_MS          5000U

static bool GD30BM2016_ReadRegister(uint16_t reg, uint32_t *value, uint8_t length);
static GD30BM2016_AlertOpsTypedf AlertOps;
static uint8_t FetRequestState;
static uint32_t BalanceRequestMask;
static uint8_t GD30BM2016_CurrentI2cAddr = GD30BM2016_I2C_ADDR;
static bool GD30BM2016_DeviceOnline;
static bool GD30BM2016_CrcMode;

GD30BM2016_SampleDataTypedef GD30BM2016_SampleData = {0};

/**
 * @brief 格式化输出GD30驱动调试日志。
 * @param tag 日志标签。
 * @param format 格式化字符串。
 * @param ... 可变格式化参数。
 * @return 无返回值。
 */
void GD30BM2016_Log(const char *tag, const char *format, ...)
{
#if (GD30BM2016_DEBUG_LEVEL != 0)
    va_list args;
    char buffer[96];
    int length;

    print_usart1("%s", tag);

    va_start(args, format);
    length = vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);

    if (length > 0) {
        buffer[sizeof(buffer) - 1U] = '\0';
        print_usart1("%s\r\n", buffer);
    } else {
        print_usart1("\r\n");
    }
#else
    (void)tag;
    (void)format;
#endif
}

static const uint8_t CellVoltageCommandMap[GD30BM2016_CELL_MAX] = {
    GD30_CMD_CELL1_VOLTAGE,
    GD30_CMD_CELL2_VOLTAGE,
    GD30_CMD_CELL3_VOLTAGE,
    GD30_CMD_CELL4_VOLTAGE,
    GD30_CMD_CELL16_VOLTAGE,
};

static const uint8_t TempCommandMap[GD30BM2016_TEMP_MAX] = {
    GD30_CMD_TS1_TEMPERATURE,
#if (GD30BM2016_TEMP_MAX > 1)
    GD30_CMD_TS2_TEMPERATURE,
#endif
#if (GD30BM2016_TEMP_MAX > 2)
    GD30_CMD_TS3_TEMPERATURE,
#endif
};

static const uint32_t BalanceBitMap[GD30BM2016_CELL_MAX] = {
    (1UL << 0),
    (1UL << 1),
    (1UL << 2),
    (1UL << 3),
    (1UL << 16),
};

/**
 * @brief 计算GD30通信使用的CRC8校验值。
 * @param data 待计算 CRC 的只读数据缓冲区。
 * @param length 数据长度。
 * @return 返回计算得到的校验值。
 */
static uint8_t GD30BM2016_Crc8(const uint8_t *data, uint8_t length)
{
    uint8_t crc = 0;

    while (length-- != 0U) {
        for (uint8_t bit = 0x80U; bit != 0U; bit >>= 1) {
            if ((crc & 0x80U) != 0U) {
                crc <<= 1;
                crc ^= 0x07U;
            } else {
                crc <<= 1;
            }

            if ((*data & bit) != 0U) {
                crc ^= 0x07U;
            }
        }
        data++;
    }

    return crc;
}

/**
 * @brief 组装并发送GD30底层写帧，可选CRC和末字节NACK兼容。
 * @param command GD30命令码。
 * @param data 待发送的只读数据缓冲区。
 * @param length 数据长度。
 * @param use_crc 是否启用CRC校验。
 * @param allow_final_nack 是否接受末字节NACK。
 * @param acked_len 实际应答长度输出地址。
 * @return true表示操作成功，false表示操作失败。
 */
static bool GD30BM2016_WriteFrame(uint8_t command, const uint8_t *data, uint8_t length,
                                  bool use_crc, bool allow_final_nack, uint16_t *acked_len)
{
    uint8_t tx[12];
    uint8_t crc_data[12];
    uint8_t tx_len;
    struct I2C_MessageTypeDef msg = {0};

    if (acked_len != NULL) {
        *acked_len = 0U;
    }

    if (data == NULL || length > (sizeof(tx) - 2U)) {
        return false;
    }

    tx[0] = command;
    memcpy(&tx[1], data, length);
    tx_len = length + 1U;

    if (use_crc) {
        uint8_t crc_length;

        crc_data[0] = (uint8_t)(GD30BM2016_CurrentI2cAddr << 1);
        memcpy(&crc_data[1], tx, tx_len);
        crc_length = tx_len + 1U;
        tx[tx_len] = GD30BM2016_Crc8(crc_data, crc_length);
        tx_len++;
    }

    msg.addr = GD30BM2016_CurrentI2cAddr;
    msg.flags = I2C_WR;
    msg.buf = tx;
    msg.tLen = tx_len;

    if (I2C_TransferMessages(&i2c1, &msg, 1) == 1U) {
        if (acked_len != NULL) {
            *acked_len = tx_len;
        }
        return true;
    }
    if (acked_len != NULL) {
        *acked_len = msg.rLen;
    }

    /* GD30 write timing diagrams mark NACK before STOP on the last byte. */
    return (allow_final_nack && msg.rLen == (uint16_t)(tx_len - 1U));
}

/**
 * @brief 按照当前CRC模式向GD30命令地址写入字节。
 * @param command GD30命令码。
 * @param data 待写入 GD30 的只读数据缓冲区。
 * @param length 数据长度。
 * @return true表示操作成功，false表示操作失败。
 */
static bool GD30BM2016_WriteBytes(uint8_t command, const uint8_t *data, uint8_t length)
{
    uint16_t acked = 0;

    if (GD30BM2016_WriteFrame(command, data, length, GD30BM2016_CrcMode, true, &acked)) {
        return true;
    }

    GD30BM2016_ERROR("write 0x%02x fail crc=%u len=%u ack=%u",
                     command,
                     (unsigned int)GD30BM2016_CrcMode,
                     (unsigned int)length,
                     (unsigned int)acked);
    return false;
}

/**
 * @brief 发送命令并读取GD30响应帧，同时执行可选CRC校验。
 * @param command GD30命令码。
 * @param data 用于保存 GD30 响应数据的缓冲区。
 * @param length 数据长度。
 * @param use_crc 是否启用CRC校验。
 * @return true表示操作成功，false表示操作失败。
 */
static bool GD30BM2016_ReadFrame(uint8_t command, uint8_t *data, uint8_t length, bool use_crc)
{
    struct I2C_MessageTypeDef msg[2] = {0};
    uint8_t cmd_tx[2];
    uint8_t rx[8];
    uint8_t crc_data[12];
    uint8_t rx_len;

    if (data == NULL || length == 0U || length > (sizeof(rx) - 1U)) {
        return false;
    }

    rx_len = length;
    if (use_crc) {
        rx_len++;
    }
    cmd_tx[0] = command;

    msg[0].addr = GD30BM2016_CurrentI2cAddr;
    msg[0].flags = I2C_WR;
    msg[0].buf = cmd_tx;
    msg[0].tLen = 1U;

    msg[1].addr = GD30BM2016_CurrentI2cAddr;
    msg[1].flags = I2C_RD;
    msg[1].buf = rx;
    msg[1].tLen = rx_len;

    if (I2C_TransferMessages(&i2c1, msg, 2) != 2U) {
        return false;
    }

    if (use_crc) {
        crc_data[0] = (uint8_t)(GD30BM2016_CurrentI2cAddr << 1);
        crc_data[1] = command;
        crc_data[2] = (uint8_t)((GD30BM2016_CurrentI2cAddr << 1) | 0x01U);
        memcpy(&crc_data[3], rx, length);

        if (GD30BM2016_Crc8(crc_data, length + 3U) != rx[length]) {
            return false;
        }
    }

    memcpy(data, rx, length);
    return true;
}

/**
 * @brief 按指定CRC模式读取GD30直接命令的16位数据。
 * @param command GD30命令码。
 * @param use_crc 是否启用CRC校验。
 * @param value 用于保存读取到的 16 位数据的输出指针。
 * @return true表示操作成功，false表示操作失败。
 */
static bool GD30BM2016_ReadDirectWordWithMode(uint8_t command, bool use_crc, uint16_t *value)
{
    uint8_t data[2];

    if (value == NULL) {
        return false;
    }

    if (!GD30BM2016_ReadFrame(command, data, 2U, use_crc)) {
        return false;
    }

    *value = (uint16_t)data[0] | ((uint16_t)data[1] << 8);
    return true;
}

/**
 * @brief 探测指定地址及其CRC读取模式。
 * @param address I2C从机地址。
 * @param battery_status 用于保存读取到的 Battery Status 的输出指针。
 * @return true表示操作成功，false表示操作失败。
 */
static bool GD30BM2016_DetectReadMode(uint8_t address, uint16_t *battery_status)
{
    uint16_t status = 0;
    uint8_t crc_ok = 0;

    GD30BM2016_CurrentI2cAddr = address;

    /*
     * A CRC-enabled GD30 can ACK a command byte even when the host omits CRC.
     * Prefer verified CRC reads, otherwise a non-CRC read can look valid while
     * later subcommands are silently ignored.
     */
    for (uint8_t retry = 0; retry < 3U; retry++) {
        if (GD30BM2016_ReadDirectWordWithMode(GD30_CMD_BATTERY_STATUS, true, &status)) {
            crc_ok++;
            if (crc_ok >= 2U) {
                GD30BM2016_CrcMode = true;
                if (battery_status != NULL) {
                    *battery_status = status;
                }
                return true;
            }
        }
        GD30BM2016_DELAY(1);
    }

    if (GD30BM2016_ReadDirectWordWithMode(GD30_CMD_BATTERY_STATUS, false, &status)) {
        GD30BM2016_CrcMode = false;
        if (battery_status != NULL) {
            *battery_status = status;
        }
        return true;
    }

    return false;
}

/**
 * @brief 按照当前CRC模式读取指定GD30命令的数据。
 * @param command GD30命令码。
 * @param data 用于保存读取数据的缓冲区。
 * @param length 数据长度。
 * @return true表示操作成功，false表示操作失败。
 */
static bool GD30BM2016_ReadBytes(uint8_t command, uint8_t *data, uint8_t length)
{
    if (GD30BM2016_ReadFrame(command, data, length, GD30BM2016_CrcMode)) {
        return true;
    }

    if (!GD30BM2016_CrcMode && GD30BM2016_ReadFrame(command, data, length, true)) {
        GD30BM2016_CrcMode = true;
        GD30BM2016_INFO("i2c crc mode enabled");
        return true;
    }

    GD30BM2016_ERROR("read 0x%02x fail crc=%u len=%u",
                     command,
                     (unsigned int)GD30BM2016_CrcMode,
                     (unsigned int)length);
    return false;
}

/**
 * @brief 计算GD30配置数据块使用的反码校验和。
 * @param data 待计算校验和的只读数据缓冲区。
 * @param length 数据长度。
 * @return 返回计算得到的校验值。
 */
static uint8_t GD30BM2016_Checksum(const uint8_t *data, uint8_t length)
{
    uint16_t checksum = 0;

    while (length-- != 0U) {
        checksum += *data++;
    }

    while (checksum > 0xFFU) {
        checksum = (checksum >> 8) + (checksum & 0xFFU);
    }

    return (uint8_t)(0xFFU - checksum);
}

/**
 * @brief 读取GD30直接命令的16位数据。
 * @param command GD30命令码。
 * @param value 用于保存读取到的 16 位数据的输出指针。
 * @return true表示操作成功，false表示操作失败。
 */
static bool GD30BM2016_ReadDirectWord(uint8_t command, uint16_t *value)
{
    uint8_t data[2];

    if (!GD30BM2016_ReadBytes(command, data, 2U)) {
        return false;
    }

    *value = (uint16_t)data[0] | ((uint16_t)data[1] << 8);
    return true;
}

/**
 * @brief 执行GD30总线时序所需的短延时。
 * @param 无。
 * @return 无返回值。
 */
static void GD30BM2016_BusDelay(void)
{
    volatile uint32_t delay = 800U;

    while (delay-- != 0U) {
        __NOP();
    }
}

/**
 * @brief 保持GD30 RST_SHUT引脚为低电平。
 * @param 无。
 * @return 无返回值。
 */
static void GD30BM2016_HoldRstShutLow(void)
{
    gpio_bit_reset(GD30BM2016_RST_SHUT_GPIO_Port, GD30BM2016_RST_SHUT_Pin);
    gpio_init(GD30BM2016_RST_SHUT_GPIO_Port, GPIO_MODE_OUT_PP, GPIO_OSPEED_50MHZ,
              GD30BM2016_RST_SHUT_Pin);
    GD30BM2016_BusDelay();
}

/**
 * @brief 配置并执行GD30硬件复位前置时序。
 * @param 无。
 * @return 无返回值。
 */
static void GD30BM2016_HardwareResetPrepare(void)
{
    gpio_init(GD30BM2016_RST_SHUT_GPIO_Port, GPIO_MODE_OUT_PP, GPIO_OSPEED_50MHZ,
              GD30BM2016_RST_SHUT_Pin);
    gpio_bit_set(GD30BM2016_RST_SHUT_GPIO_Port, GD30BM2016_RST_SHUT_Pin);
    GD30BM2016_INFO("rst_shut high, forcing shutdown for %ums",
                    (unsigned int)GD30_RST_SHUT_RESET_MS);
    GD30BM2016_DELAY(GD30_RST_SHUT_RESET_MS);

    gpio_bit_reset(GD30BM2016_RST_SHUT_GPIO_Port, GD30BM2016_RST_SHUT_Pin);
    GD30BM2016_INFO("rst_shut low, press TS2 low then release");
    GD30BM2016_DELAY(100);
}

/**
 * @brief 通过SCL和SDA时序尝试唤醒GD30。
 * @param 无。
 * @return 无返回值。
 */
static void GD30BM2016_ClockWakeup(void)
{
    gpio_init(I2C1_SCL_GPIO_Port, GPIO_MODE_OUT_OD, GPIO_OSPEED_50MHZ,
              I2C1_SCL_Pin | I2C1_SDA_Pin);
    gpio_bit_set(I2C1_SDA_GPIO_Port, I2C1_SDA_Pin);
    gpio_bit_set(I2C1_SCL_GPIO_Port, I2C1_SCL_Pin);
    GD30BM2016_BusDelay();

    for (uint8_t index = 0; index < 80U; index++) {
        gpio_bit_reset(I2C1_SCL_GPIO_Port, I2C1_SCL_Pin);
        GD30BM2016_BusDelay();
        gpio_bit_set(I2C1_SCL_GPIO_Port, I2C1_SCL_Pin);
        GD30BM2016_BusDelay();
    }

    gpio_bit_reset(I2C1_SDA_GPIO_Port, I2C1_SDA_Pin);
    GD30BM2016_BusDelay();
    gpio_bit_set(I2C1_SCL_GPIO_Port, I2C1_SCL_Pin);
    GD30BM2016_BusDelay();
    gpio_bit_set(I2C1_SDA_GPIO_Port, I2C1_SDA_Pin);
    GD30BM2016_DELAY(2);
}

/**
 * @brief 读取并打印GD30软件I2C总线电平。
 * @param stage 用于诊断日志的当前流程阶段字符串。
 * @return 无返回值。
 */
static void GD30BM2016_LogBusState(const char *stage)
{
    uint8_t scl = (gpio_input_bit_get(I2C1_SCL_GPIO_Port, I2C1_SCL_Pin) == SET) ? 1U : 0U;
    uint8_t sda = (gpio_input_bit_get(I2C1_SDA_GPIO_Port, I2C1_SDA_Pin) == SET) ? 1U : 0U;
    uint8_t rst_shut = (gpio_input_bit_get(GD30BM2016_RST_SHUT_GPIO_Port,
                                           GD30BM2016_RST_SHUT_Pin) == SET) ? 1U : 0U;

    GD30BM2016_INFO("i2c %s scl=%u sda=%u rst_shut=%u addr7=0x%02X write8=0x%02X",
                    stage,
                    (unsigned int)scl,
                    (unsigned int)sda,
                    (unsigned int)rst_shut,
                    (unsigned int)GD30BM2016_CurrentI2cAddr,
                    (unsigned int)(GD30BM2016_CurrentI2cAddr << 1));
}

/**
 * @brief 向指定地址发送GD30唤醒脉冲。
 * @param address I2C从机地址。
 * @return 无返回值。
 */
static void GD30BM2016_SendWakePulse(uint8_t address)
{
    uint8_t dummy = 0;
    struct I2C_MessageTypeDef msg = {0};

    msg.addr = address;
    msg.flags = I2C_WR | I2C_IGNORE_NACK;
    msg.buf = &dummy;
    msg.tLen = 1U;

    (void)I2C_TransferMessages(&i2c1, &msg, 1U);
}

/**
 * @brief 探测指定I2C地址是否应答。
 * @param address I2C从机地址。
 * @return true表示操作成功，false表示操作失败。
 */
static bool GD30BM2016_AddressAck(uint8_t address)
{
    uint8_t dummy = 0;
    struct I2C_MessageTypeDef msg = {0};

    msg.addr = address;
    msg.flags = I2C_WR;
    msg.buf = &dummy;
    msg.tLen = 0U;

    return (I2C_TransferMessages(&i2c1, &msg, 1U) == 1U);
}

/**
 * @brief 按指定CRC模式探测命令字节应答情况。
 * @param address I2C从机地址。
 * @param command GD30命令码。
 * @param use_crc 是否启用CRC校验。
 * @param acked_len 实际应答长度输出地址。
 * @return true表示操作成功，false表示操作失败。
 */
static bool GD30BM2016_CommandAck(uint8_t address, uint8_t command, bool use_crc, uint16_t *acked_len)
{
    uint8_t tx[2];
    uint8_t crc_data[2];
    struct I2C_MessageTypeDef msg = {0};

    if (acked_len != NULL) {
        *acked_len = 0U;
    }

    tx[0] = command;
    msg.tLen = 1U;
    if (use_crc) {
        crc_data[0] = (uint8_t)(address << 1);
        crc_data[1] = command;
        tx[1] = GD30BM2016_Crc8(crc_data, 2U);
        msg.tLen = 2U;
    }

    msg.addr = address;
    msg.flags = I2C_WR;
    msg.buf = tx;

    if (I2C_TransferMessages(&i2c1, &msg, 1U) == 1U) {
        if (acked_len != NULL) {
            *acked_len = msg.tLen;
        }
        return true;
    }

    if (acked_len != NULL) {
        *acked_len = msg.rLen;
    }

    return false;
}

/**
 * @brief 在候选地址和CRC模式中探测可用GD30设备。
 * @param 无。
 * @return true表示操作成功，false表示操作失败。
 */
static bool GD30BM2016_ProbeAddress(void)
{
    static const uint8_t candidate_addr[] = {
        GD30BM2016_I2C_ADDR,    /* SPI_MOSI floating/low: 8-bit write address 0x20. */
        0x11U,                  /* SPI_MOSI high: 8-bit write address 0x22. */
        0x20U,                  /* Fallback if an external tool uses 0x20 as 7-bit. */
        0x22U,
    };

    for (uint8_t retry = 0; retry < 5U; retry++) {
        for (uint8_t index = 0; index < (uint8_t)(sizeof(candidate_addr) / sizeof(candidate_addr[0])); index++) {
            uint16_t battery_status = 0;
            bool address_ack = GD30BM2016_AddressAck(candidate_addr[index]);

            if (GD30BM2016_DetectReadMode(candidate_addr[index], &battery_status)) {
                GD30BM2016_INFO("i2c read ok addr7=0x%02X write8=0x%02X crc=%u status=0x%04X",
                                (unsigned int)GD30BM2016_CurrentI2cAddr,
                                (unsigned int)(GD30BM2016_CurrentI2cAddr << 1),
                                (unsigned int)GD30BM2016_CrcMode,
                                (unsigned int)battery_status);
                return true;
            }

            if (address_ack) {
                GD30BM2016_WARNING("addr ack only addr7=0x%02X write8=0x%02X",
                                   (unsigned int)candidate_addr[index],
                                   (unsigned int)(candidate_addr[index] << 1));
            }
        }

        GD30BM2016_SendWakePulse(GD30BM2016_I2C_ADDR);
        GD30BM2016_SendWakePulse(0x11U);
        GD30BM2016_DELAY(10);
    }

    GD30BM2016_WARNING("no gd30 command ack at write8 0x20 or 0x22");
    GD30BM2016_LogBusState("noack");
    return false;
}

/**
 * @brief 扫描I2C地址并打印应答设备。
 * @param 无。
 * @return 无返回值。
 */
static void GD30BM2016_ScanI2cAddress(void)
{
    bool found = false;
    uint32_t old_retries = i2c1.retries;

    i2c1.retries = 0U;
    for (uint8_t address = 0x08U; address <= 0x77U; address++) {
        uint16_t acked = 0;

        if (GD30BM2016_CommandAck(address, GD30_CMD_BATTERY_STATUS, false, &acked)) {
            GD30BM2016_INFO("i2c scan command ack addr7=0x%02X write8=0x%02X",
                            (unsigned int)address,
                            (unsigned int)(address << 1));
            found = true;
        } else if (GD30BM2016_AddressAck(address)) {
            GD30BM2016_WARNING("i2c scan addr-only ack addr7=0x%02X write8=0x%02X cmd_ack=%u",
                               (unsigned int)address,
                               (unsigned int)(address << 1),
                               (unsigned int)acked);
        }
    }
    i2c1.retries = old_retries;

    if (!found) {
        GD30BM2016_WARNING("i2c scan found no device");
    }
}

/**
 * @brief 发送无需返回数据的GD30子命令并记录错误。
 * @param command GD30命令码。
 * @return true表示操作成功，false表示操作失败。
 */
static bool GD30BM2016_OnlySubcommand(uint16_t command)
{
    uint8_t data[2];
    uint16_t acked = 0;

    data[0] = (uint8_t)(command & 0xFFU);
    data[1] = (uint8_t)((command >> 8) & 0xFFU);

    if (!GD30BM2016_WriteFrame(0x3EU, data, 2U, GD30BM2016_CrcMode, true, &acked)) {
        GD30BM2016_ERROR("subcmd 0x%04X fail ack=%u crc=%u",
                         (unsigned int)command,
                         (unsigned int)acked,
                         (unsigned int)GD30BM2016_CrcMode);
        return false;
    }

    GD30BM2016_DELAY(1);
    return true;
}

/**
 * @brief 静默发送无需返回数据的GD30子命令。
 * @param command GD30命令码。
 * @return true表示操作成功，false表示操作失败。
 */
static bool GD30BM2016_OnlySubcommandQuiet(uint16_t command)
{
    uint8_t data[2];

    data[0] = (uint8_t)(command & 0xFFU);
    data[1] = (uint8_t)((command >> 8) & 0xFFU);

    if (GD30BM2016_WriteFrame(0x3EU, data, 2U, GD30BM2016_CrcMode, true, NULL)) {
        return true;
    }

    return false;
}

/**
 * @brief 静默读取GD30直接命令的16位数据。
 * @param command GD30命令码。
 * @param value 用于保存读取到的 16 位数据的输出指针。
 * @return true表示操作成功，false表示操作失败。
 */
static bool GD30BM2016_ReadDirectWordQuiet(uint8_t command, uint16_t *value)
{
    uint8_t data[2];

    if (value == NULL) {
        return false;
    }

    if (!GD30BM2016_ReadFrame(command, data, 2U, GD30BM2016_CrcMode)) {
        if (GD30BM2016_CrcMode ||
            !GD30BM2016_ReadFrame(command, data, 2U, true)) {
            return false;
        }
        GD30BM2016_CrcMode = true;
    }

    *value = (uint16_t)data[0] | ((uint16_t)data[1] << 8);
    return true;
}

/**
 * @brief 静默读取GD30直接命令的8位数据。
 * @param command GD30命令码。
 * @param value 用于保存读取到的 8 位数据的输出指针。
 * @return true表示操作成功，false表示操作失败。
 */
static bool GD30BM2016_ReadDirectByteQuiet(uint8_t command, uint8_t *value)
{
    uint8_t data = 0;

    if (value == NULL) {
        return false;
    }

    if (!GD30BM2016_ReadFrame(command, &data, 1U, GD30BM2016_CrcMode)) {
        if (GD30BM2016_CrcMode ||
            !GD30BM2016_ReadFrame(command, &data, 1U, true)) {
            return false;
        }
        GD30BM2016_CrcMode = true;
    }

    *value = data;
    return true;
}

/**
 * @brief 通过配置数据窗口写入GD30寄存器。
 * @param reg GD30寄存器或子命令地址。
 * @param value 要写入寄存器的数值。
 * @param length 数据长度。
 * @return true表示操作成功，false表示操作失败。
 */
static bool GD30BM2016_WriteRegister(uint16_t reg, uint32_t value, uint8_t length)
{
    uint8_t addr[2];
    uint8_t data[4];
    uint8_t checksum_len[2];

    if (length != 1U && length != 2U && length != 4U) {
        return false;
    }

    addr[0] = (uint8_t)(reg & 0xFFU);
    addr[1] = (uint8_t)((reg >> 8) & 0xFFU);

    for (uint8_t index = 0; index < length; index++) {
        data[index] = (uint8_t)((value >> (8U * index)) & 0xFFU);
    }

    if (!GD30BM2016_WriteBytes(0x3EU, addr, 2U)) {
        return false;
    }
    GD30BM2016_DELAY(1);

    checksum_len[0] = GD30BM2016_Checksum(data, length);
    checksum_len[1] = (uint8_t)(0x80U | length);
    if (!GD30BM2016_WriteBytes(0x60U, checksum_len, 2U)) {
        return false;
    }
    GD30BM2016_DELAY(1);

    if (!GD30BM2016_WriteBytes(0x40U, data, length)) {
        return false;
    }
    GD30BM2016_DELAY(1);

    return true;
}

/**
 * @brief 发送GD30子命令并读取16位返回值。
 * @param command GD30命令码。
 * @param value 用于保存子命令返回值的输出指针。
 * @return true表示操作成功，false表示操作失败。
 */
static bool GD30BM2016_ReadSubcommandWord(uint16_t command, uint16_t *value)
{
    uint8_t data[2];
    uint32_t result = 0U;

    if (value == NULL) {
        return false;
    }

    data[0] = (uint8_t)(command & 0xFFU);
    data[1] = (uint8_t)((command >> 8) & 0xFFU);

    if (!GD30BM2016_WriteBytes(0x3EU, data, 2U)) {
        return false;
    }
    GD30BM2016_DELAY(1);

    if (!GD30BM2016_ReadRegister(command, &result, 2U)) {
        return false;
    }

    *value = (uint16_t)result;
    return true;
}
/**
 * @brief 通过配置数据窗口读取GD30寄存器。
 * @param reg GD30寄存器或子命令地址。
 * @param value 用于保存寄存器值的输出指针。
 * @param length 数据长度。
 * @return true表示操作成功，false表示操作失败。
 */
static bool GD30BM2016_ReadRegister(uint16_t reg, uint32_t *value, uint8_t length)
{
    uint8_t addr[2];
    uint8_t data[4];
    uint8_t len_req;
    uint32_t result = 0;

    if (value == NULL || (length != 1U && length != 2U && length != 4U)) {
        return false;
    }

    addr[0] = (uint8_t)(reg & 0xFFU);
    addr[1] = (uint8_t)((reg >> 8) & 0xFFU);
    len_req = length;

    if (!GD30BM2016_WriteBytes(0x3EU, addr, 2U)) {
        return false;
    }
    GD30BM2016_DELAY(1);

    if (!GD30BM2016_WriteBytes(0x61U, &len_req, 1U)) {
        return false;
    }
    GD30BM2016_DELAY(1);

    if (!GD30BM2016_ReadBytes(0x40U, data, length)) {
        return false;
    }

    for (uint8_t index = 0; index < length; index++) {
        result |= ((uint32_t)data[index] << (8U * index));
    }
    *value = result;

    return true;
}

/**
 * @brief 轮询等待GD30进入正常工作模式。
 * @param stage 用于诊断日志的当前流程阶段字符串。
 * @return true表示操作成功，false表示操作失败。
 */
static bool GD30BM2016_WaitNormalMode(const char *stage)
{
    uint16_t battery_status = 0;

    for (uint8_t retry = 0; retry < 12U; retry++) {
        (void)GD30BM2016_OnlySubcommand(GD30_SUBCMD_SLEEP_DISABLE);
        GD30BM2016_DELAY(2);
        (void)GD30BM2016_OnlySubcommand(GD30_SUBCMD_SLEEP_DISABLE);
        GD30BM2016_DELAY(20);

        if (GD30BM2016_ReadDirectWord(GD30_CMD_BATTERY_STATUS, &battery_status)) {
            if ((battery_status & (GD30_BATTERY_STATUS_SLEEP_BIT |
                                   GD30_BATTERY_STATUS_SLEEP_EN_BIT)) == 0U) {
                GD30BM2016_INFO("%s normal status=0x%04X", stage, (unsigned int)battery_status);
                return true;
            }
        }
    }

    GD30BM2016_WARNING("%s still sleep status=0x%04X", stage, (unsigned int)battery_status);
    return false;
}

/**
 * @brief 关闭GD30快速休眠功能。
 * @param stage 用于诊断日志的当前流程阶段字符串。
 * @return true表示操作成功，false表示操作失败。
 */
static bool GD30BM2016_DisableSleepFast(const char *stage)
{
    uint16_t battery_status = 0;

    for (uint8_t burst = 0; burst < 8U; burst++) {
        (void)GD30BM2016_OnlySubcommandQuiet(GD30_SUBCMD_SLEEP_DISABLE);
        GD30BM2016_DELAY(1);
    }

    for (uint8_t retry = 0; retry < 20U; retry++) {
        (void)GD30BM2016_OnlySubcommandQuiet(GD30_SUBCMD_SLEEP_DISABLE);
        GD30BM2016_DELAY(2);

        if (GD30BM2016_ReadDirectWordQuiet(GD30_CMD_BATTERY_STATUS, &battery_status) &&
            ((battery_status & (GD30_BATTERY_STATUS_SLEEP_BIT |
                                GD30_BATTERY_STATUS_SLEEP_EN_BIT)) == 0U)) {
            GD30BM2016_INFO("%s sleep disabled status=0x%04X",
                            stage,
                            (unsigned int)battery_status);
            return true;
        }
    }

    GD30BM2016_WARNING("%s sleep disable failed status=0x%04X",
                       stage,
                       (unsigned int)battery_status);
    return false;
}

/**
 * @brief 读取并解析GD30安全访问状态。
 * @param battery_status GD30 的 Battery Status 寄存器值。
 * @return 返回从 Battery Status 中解析出的安全状态编码。
 */
static uint8_t GD30BM2016_GetSecurityState(uint16_t battery_status)
{
    return (uint8_t)((battery_status & GD30_BATTERY_STATUS_SEC_MASK) >> 8);
}

/**
 * @brief 轮询等待GD30完成内部初始化。
 * @param 无。
 * @return true表示操作成功，false表示操作失败。
 */
static bool GD30BM2016_WaitInitialized(void)
{
    uint16_t battery_status = 0;
    uint16_t alarm_raw = 0;

    for (uint32_t elapsed = 0; elapsed < GD30_INIT_WAIT_MS; elapsed += 100U) {
        (void)GD30BM2016_OnlySubcommandQuiet(GD30_SUBCMD_SLEEP_DISABLE);
        GD30BM2016_DELAY(100);

        (void)GD30BM2016_ReadDirectWordQuiet(GD30_CMD_ALARM_RAW_STATUS, &alarm_raw);
        if (GD30BM2016_ReadDirectWordQuiet(GD30_CMD_BATTERY_STATUS, &battery_status) &&
            ((alarm_raw & GD30_ALARM_INITCOMP_BIT) != 0U)) {
            GD30BM2016_INFO("init ready status=0x%04X alarm_raw=0x%04X sec=%u",
                            (unsigned int)battery_status,
                            (unsigned int)alarm_raw,
                            (unsigned int)GD30BM2016_GetSecurityState(battery_status));
            return true;
        }
    }

    GD30BM2016_ERROR("init not ready status=0x%04X alarm_raw=0x%04X",
                     (unsigned int)battery_status,
                     (unsigned int)alarm_raw);
    return false;
}

/**
 * @brief 轮询等待GD30安全配置加载完成。
 * @param 无。
 * @return true表示操作成功，false表示操作失败。
 */
static bool GD30BM2016_WaitSecurityLoaded(void)
{
    uint8_t control_status = 0;
    uint16_t battery_status = 0;
    uint16_t alarm_raw = 0;
    uint32_t security_settings = 0;

    for (uint32_t elapsed = 0; elapsed < GD30_SECURITY_WAIT_MS; elapsed += 100U) {
        (void)GD30BM2016_OnlySubcommandQuiet(GD30_SUBCMD_SLEEP_DISABLE);
        GD30BM2016_DELAY(100);

        (void)GD30BM2016_ReadDirectWordQuiet(GD30_CMD_ALARM_RAW_STATUS, &alarm_raw);
        (void)GD30BM2016_ReadDirectByteQuiet(GD30_CMD_CONTROL_STATUS, &control_status);
        if (GD30BM2016_ReadDirectWordQuiet(GD30_CMD_BATTERY_STATUS, &battery_status) &&
            ((alarm_raw & GD30_ALARM_INITCOMP_BIT) != 0U) &&
            ((control_status & (GD30_CONTROL_STATUS_AUTO_LOAD_TIMEOUT_BIT |
                                GD30_CONTROL_STATUS_DEEPSLEEP_BIT)) == 0U)) {
            (void)GD30BM2016_ReadRegister(GD30_REG_SECURITY_SETTINGS, &security_settings, 1U);
            GD30BM2016_INFO("security ready bat=0x%04X ctrl=0x%02X raw=0x%04X set=0x%02X",
                            (unsigned int)battery_status,
                            (unsigned int)control_status,
                            (unsigned int)alarm_raw,
                            (unsigned int)security_settings);
            return true;
        }
    }

    (void)GD30BM2016_ReadRegister(GD30_REG_SECURITY_SETTINGS, &security_settings, 1U);
    GD30BM2016_WARNING("security wait bat=0x%04X ctrl=0x%02X raw=0x%04X set=0x%02X",
                       (unsigned int)battery_status,
                       (unsigned int)control_status,
                       (unsigned int)alarm_raw,
                       (unsigned int)security_settings);
    return true;
}

/**
 * @brief 读取电池状态并输出带阶段标签的日志。
 * @param stage 用于诊断日志的当前流程阶段字符串。
 * @param battery_status 用于保存读取到的 Battery Status 的输出指针。
 * @return true表示操作成功，false表示操作失败。
 */
static bool GD30BM2016_ReadBatteryStatusLogged(const char *stage, uint16_t *battery_status)
{
    uint16_t status = 0;

    if (!GD30BM2016_ReadDirectWord(GD30_CMD_BATTERY_STATUS, &status)) {
        return false;
    }

    GD30BM2016_INFO("%s status=0x%04X sec=%u",
                    stage,
                    (unsigned int)status,
                    (unsigned int)GD30BM2016_GetSecurityState(status));
    if (battery_status != NULL) {
        *battery_status = status;
    }
    return true;
}

/**
 * @brief 尝试发送命令使GD30进入配置更新模式。
 * @param stage 用于诊断日志的当前流程阶段字符串。
 * @param battery_status 用于保存进入配置模式后 Battery Status 的输出指针。
 * @return true表示操作成功，false表示操作失败。
 */
static bool GD30BM2016_TrySetCfgUpdate(const char *stage, uint16_t *battery_status)
{
    uint16_t status = 0;

    for (uint8_t attempt = 0; attempt < 3U; attempt++) {
        if (!GD30BM2016_OnlySubcommand(GD30_SUBCMD_SET_CFGUPDATE)) {
            return false;
        }

        for (uint8_t retry = 0; retry < 40U; retry++) {
            GD30BM2016_DELAY(10);
            if (GD30BM2016_ReadDirectWord(GD30_CMD_BATTERY_STATUS, &status) &&
                ((status & GD30_BATTERY_STATUS_CFGUPDATE_BIT) != 0U)) {
                GD30BM2016_INFO("%s cfgupdate entered status=0x%04X",
                                stage,
                                (unsigned int)status);
                if (battery_status != NULL) {
                    *battery_status = status;
                }
                return true;
            }
        }

        GD30BM2016_WARNING("%s cfgupdate retry %u status=0x%04X",
                           stage,
                           (unsigned int)(attempt + 1U),
                           (unsigned int)status);

        if (!GD30BM2016_CrcMode &&
            GD30BM2016_DetectReadMode(GD30BM2016_CurrentI2cAddr, &status) &&
            GD30BM2016_CrcMode) {
            GD30BM2016_WARNING("%s switched to crc mode status=0x%04X",
                               stage,
                               (unsigned int)status);
        }
    }

    if (battery_status != NULL) {
        *battery_status = status;
    }
    return false;
}

/**
 * @brief 写入GD30访问密钥序列。
 * @param stage 用于诊断日志的当前流程阶段字符串。
 * @return true表示操作成功，false表示操作失败。
 */
static bool GD30BM2016_WriteAccessKeySequence(const char *stage)
{
    if (!GD30BM2016_WriteRegister(GD30_SUBCMD_ACCESS_KEY_STEP1, 0x5A5A5A5AUL, 4U)) {
        return false;
    }
    GD30BM2016_DELAY(1);

    if (!GD30BM2016_WriteRegister(GD30_SUBCMD_ACCESS_KEY_STEP1, 0xA5A5A5A5UL, 4U)) {
        return false;
    }
    GD30BM2016_DELAY(20);

    (void)GD30BM2016_ReadBatteryStatusLogged(stage, NULL);
    return true;
}

/**
 * @brief 执行访问密钥流程使GD30进入Full Access状态。
 * @param 无。
 * @return true表示操作成功，false表示操作失败。
 */
static bool GD30BM2016_EnterFullAccess(void)
{
    uint16_t battery_status = 0;

    (void)GD30BM2016_ReadBatteryStatusLogged("security before", &battery_status);

    if (!GD30BM2016_WriteAccessKeySequence("security accesskey")) {
        return false;
    }

    return true;
}

/**
 * @brief 读取并打印阻止进入配置模式的诊断状态。
 * @param 无。
 * @return 无返回值。
 */
static void GD30BM2016_LogCfgBlockers(void)
{
    uint8_t control_status = 0;
    uint16_t alarm_status = 0;
    uint16_t alarm_raw = 0;
    uint16_t battery_status = 0;
    uint32_t security_settings = 0;
    uint8_t safety_a = 0;
    uint8_t safety_b = 0;
    uint8_t safety_c = 0;
    uint8_t pf_a = 0;
    uint8_t pf_b = 0;
    uint8_t pf_c = 0;
    uint8_t fet_status = 0;
    uint16_t manufacturing_status = 0;

    (void)GD30BM2016_ReadDirectByteQuiet(GD30_CMD_CONTROL_STATUS, &control_status);
    (void)GD30BM2016_ReadDirectWordQuiet(GD30_CMD_BATTERY_STATUS, &battery_status);
    (void)GD30BM2016_ReadDirectWordQuiet(GD30_CMD_ALARM_STATUS, &alarm_status);
    (void)GD30BM2016_ReadDirectWordQuiet(GD30_CMD_ALARM_RAW_STATUS, &alarm_raw);
    (void)GD30BM2016_ReadRegister(GD30_REG_SECURITY_SETTINGS, &security_settings, 1U);
    (void)GD30BM2016_ReadDirectByteQuiet(GD30_CMD_SAFETY_STATUS_A, &safety_a);
    (void)GD30BM2016_ReadDirectByteQuiet(GD30_CMD_SAFETY_STATUS_B, &safety_b);
    (void)GD30BM2016_ReadDirectByteQuiet(GD30_CMD_SAFETY_STATUS_C, &safety_c);
    (void)GD30BM2016_ReadDirectByteQuiet(GD30_CMD_PF_STATUS_A, &pf_a);
    (void)GD30BM2016_ReadDirectByteQuiet(GD30_CMD_PF_STATUS_B, &pf_b);
    (void)GD30BM2016_ReadDirectByteQuiet(GD30_CMD_PF_STATUS_C, &pf_c);
    (void)GD30BM2016_ReadDirectByteQuiet(GD30_CMD_FET_STATUS, &fet_status);
    (void)GD30BM2016_ReadSubcommandWord(GD30_SUBCMD_MANUFACTURING_STATUS, &manufacturing_status);

    GD30BM2016_WARNING("diag bat=0x%04X ctrl=0x%02X sec_set=0x%02X",
                       (unsigned int)battery_status,
                       (unsigned int)control_status,
                       (unsigned int)security_settings);
    GD30BM2016_WARNING("diag alarm=0x%04X raw=0x%04X fet=0x%02X manu=0x%04X",
                       (unsigned int)alarm_status,
                       (unsigned int)alarm_raw,
                       (unsigned int)fet_status,
                       (unsigned int)manufacturing_status);
    GD30BM2016_WARNING("diag safety=%02X/%02X/%02X pf=%02X/%02X/%02X",
                       (unsigned int)safety_a,
                       (unsigned int)safety_b,
                       (unsigned int)safety_c,
                       (unsigned int)pf_a,
                       (unsigned int)pf_b,
                       (unsigned int)pf_c);
}

/**
 * @brief 连续发送GD30芯片复位命令。
 * @param 无。
 * @return 无返回值。
 */
static void GD30BM2016_ChipResetSequence(void)
{
    for (uint8_t retry = 0; retry < 3U; retry++) {
        (void)GD30BM2016_OnlySubcommand(GD30_SUBCMD_CHIP_RESET);
        GD30BM2016_DELAY(20);
    }

    GD30BM2016_INFO("chip reset sequence sent");
    GD30BM2016_DELAY(350);
}

/**
 * @brief 等待用户通过TS2完成GD30手动唤醒。
 * @param 无。
 * @return true表示操作成功，false表示操作失败。
 */
static bool GD30BM2016_WaitManualTs2Wake(void)
{
    uint16_t battery_status = 0;

    GD30BM2016_WARNING("waiting %ums for TS2 falling edge",
                       (unsigned int)GD30_TS2_WAKE_WAIT_MS);
    GD30BM2016_WARNING("press TS2 button once now");

    for (uint32_t elapsed = 0; elapsed < GD30_TS2_WAKE_WAIT_MS; elapsed += GD30_TS2_WAKE_POLL_MS) {
        (void)GD30BM2016_OnlySubcommandQuiet(GD30_SUBCMD_SLEEP_DISABLE);
        GD30BM2016_DELAY(GD30_TS2_WAKE_POLL_MS);

        if (GD30BM2016_ReadDirectWordQuiet(GD30_CMD_BATTERY_STATUS, &battery_status) &&
            ((battery_status & GD30_BATTERY_STATUS_SLEEP_BIT) == 0U)) {
            GD30BM2016_INFO("ts2 wake ok status=0x%04X", (unsigned int)battery_status);
            if (GD30BM2016_DisableSleepFast("after ts2")) {
                return true;
            }
        }
    }

    GD30BM2016_WARNING("ts2 wake timeout status=0x%04X", (unsigned int)battery_status);
    return false;
}

/**
 * @brief 等待TS2唤醒后GD30地址开始应答。
 * @param 无。
 * @return true表示操作成功，false表示操作失败。
 */
static bool GD30BM2016_WaitManualTs2Address(void)
{
    GD30BM2016_WARNING("waiting %ums for TS2 wake and i2c ack",
                       (unsigned int)GD30_TS2_WAKE_WAIT_MS);
    GD30BM2016_WARNING("press TS2 button once now");

    for (uint32_t elapsed = 0; elapsed < GD30_TS2_WAKE_WAIT_MS; elapsed += GD30_TS2_WAKE_POLL_MS) {
        GD30BM2016_ClockWakeup();
        GD30BM2016_SendWakePulse(GD30BM2016_I2C_ADDR);
        GD30BM2016_SendWakePulse(0x11U);

        if (GD30BM2016_ProbeAddress()) {
            return true;
        }

        GD30BM2016_DELAY(GD30_TS2_WAKE_POLL_MS);
    }

    GD30BM2016_WARNING("ts2 i2c ack timeout");
    return false;
}

/**
 * @brief 完成前置检查并使GD30进入配置更新模式。
 * @param 无。
 * @return true表示操作成功，false表示操作失败。
 */
static bool GD30BM2016_EnterCfgUpdate(void)
{
    uint16_t battery_status = 0;

    if (!GD30BM2016_WaitNormalMode("before cfg")) {
        return false;
    }
    if (!GD30BM2016_WaitInitialized()) {
        return false;
    }
    (void)GD30BM2016_WaitSecurityLoaded();

    if (GD30BM2016_TrySetCfgUpdate("direct", &battery_status)) {
        return true;
    }

    if (!GD30BM2016_EnterFullAccess()) {
        return false;
    }
    if (GD30BM2016_TrySetCfgUpdate("accesskey", &battery_status)) {
        return true;
    }

    if (!GD30BM2016_OnlySubcommand(GD30_SUBCMD_SEAL)) {
        return false;
    }
    GD30BM2016_DELAY(1);
    if (!GD30BM2016_WriteAccessKeySequence("security seal+key")) {
        return false;
    }
    if (GD30BM2016_TrySetCfgUpdate("seal+key", &battery_status)) {
        return true;
    }

    GD30BM2016_LogCfgBlockers();
    GD30BM2016_ERROR("cfgupdate not entered status=0x%04X", (unsigned int)battery_status);
    return false;
}

/**
 * @brief 将毫伏值转换为GD30单体过压阈值编码。
 * @param millivolt 单体过压阈值，单位为毫伏。
 * @return 返回 GD30 单体过压阈值寄存器编码。
 */
static uint16_t GD30BM2016_MillivoltToCovCode(uint16_t millivolt)
{
    if (millivolt < 20U) {
        return 0U;
    }

    return (uint16_t)((millivolt / 20U) - 1U);
}

/**
 * @brief 将毫伏值转换为GD30单体欠压阈值编码。
 * @param millivolt 单体欠压阈值，单位为毫伏。
 * @return 返回 GD30 单体欠压阈值寄存器编码。
 */
static uint16_t GD30BM2016_MillivoltToCuvCode(uint16_t millivolt)
{
    if (millivolt < 20U) {
        return 0U;
    }

    return (uint16_t)((millivolt / 20U) - 1U);
}

/**
 * @brief 将秒数转换为GD30延时寄存器编码。
 * @param seconds 目标保护延时时间，单位为秒。
 * @return 返回 GD30 比较器延时寄存器编码。
 */
static uint16_t GD30BM2016_SecondDelayCode(uint8_t seconds)
{
    /* COV/CUV/OCC/OCD comparator delays use 3.9 ms loops with a 2-loop offset. */
    uint32_t code = ((uint32_t)seconds * 10000UL + 19U) / 39U;

    if (code <= 2U) {
        code = 0U;
    } else {
        code -= 2U;
    }
    if (code > 0xFFFFU) {
        code = 0xFFFFU;
    }

    return (uint16_t)code;
}

/**
 * @brief 将过压延时枚举转换为秒。
 * @param delay 保护延时配置。
 * @return 返回过压延时对应的秒数。
 */
static uint8_t GD30BM2016_OvDelayToSeconds(GD30BM2016_OVDelayTypedef delay)
{
    static const uint8_t seconds[] = {1U, 2U, 4U, 8U};

    return seconds[(uint8_t)delay & 0x03U];
}

/**
 * @brief 将欠压延时枚举转换为秒。
 * @param delay 保护延时配置。
 * @return 返回欠压延时对应的秒数。
 */
static uint8_t GD30BM2016_UvDelayToSeconds(GD30BM2016_UVDelayTypedef delay)
{
    static const uint8_t seconds[] = {1U, 4U, 8U, 16U};

    return seconds[(uint8_t)delay & 0x03U];
}

/**
 * @brief 将放电过流延时枚举转换为寄存器编码。
 * @param delay 保护延时配置。
 * @return 返回 GD30 放电过流延时寄存器编码。
 */
static uint8_t GD30BM2016_OcdDelayToRegister(GD30BM2016_OCDDelayTypedef delay)
{
    static const uint16_t ms[] = {10U, 20U, 40U, 80U, 160U, 320U, 640U, 1280U};
    uint32_t code = ((uint32_t)ms[(uint8_t)delay & 0x07U] * 10U + 19U) / 39U;

    if (code <= 2U) {
        code = 0U;
    } else {
        code -= 2U;
    }
    if (code > 0xFFU) {
        code = 0xFFU;
    }

    return (uint8_t)code;
}

/**
 * @brief 将短路延时枚举转换为寄存器编码。
 * @param delay 保护延时配置。
 * @return 返回 GD30 短路延时寄存器编码。
 */
static uint8_t GD30BM2016_ScdDelayToRegister(GD30BM2016_SCDDelayTypedef delay)
{
    static const uint16_t us[] = {50U, 100U, 200U, 400U};
    uint32_t code = (((uint32_t)us[(uint8_t)delay & 0x03U] * 4U) + 62U) / 125U;

    code += 1U;

    if (code == 0U) {
        code = 1U;
    }
    if (code > 0xFFU) {
        code = 0xFFU;
    }

    return (uint8_t)code;
}

/**
 * @brief 将电流阈值转换为GD30过流寄存器编码。
 * @param current_amp 目标过流阈值，单位为安培。
 * @return 返回基于采样电阻换算的 GD30 过流阈值寄存器编码。
 */
static uint16_t GD30BM2016_OverCurrentToRegister(float current_amp)
{
    uint32_t steps;
    float step_count;

    if (current_amp <= 0.0f) {
        return 0U;
    }

    /* OCC/OCD threshold current is (N + 1) * 2 mV / Rsns.
       Use the first hardware threshold that is not lower than the
       configured software limit. */
    step_count = (current_amp * GD30BM2016_SHUNT_RESISTOR_MOHM) / 2.0f;
    steps = (uint32_t)step_count;
    if ((float)steps < step_count) {
        steps++;
    }
    if (steps == 0U) {
        steps = 1U;
    }
    if (steps > 0x10000UL) {
        steps = 0x10000UL;
    }

    return (uint16_t)(steps - 1U);
}

/**
 * @brief 将32位数值饱和限制为8位范围。
 * @param value 待限制的 32 位无符号数值。
 * @param min 允许的最小值。
 * @param max 允许的最大值。
 * @return 返回限制在 min～max 范围内的 8 位数值。
 */
static uint8_t GD30BM2016_LimitU8(uint32_t value, uint8_t min, uint8_t max)
{
    if (value < min) {
        return min;
    }
    if (value > max) {
        return max;
    }
    return (uint8_t)value;
}
/**
 * @brief 向GD30写入电芯、采样、保护、FET和均衡配置。
 * @param config 要写入 GD30 的 BMS 配置数据。
 * @return true表示操作成功，false表示操作失败。
 */
static bool GD30BM2016_ApplyConfig(const GD30BM2016_ConfigDataTypedef *config)
{
    bool ok = true;
    uint8_t balance_interval = GD30BM2016_LimitU8(EXTERNAL_BALANCE_AFE_INTERVAL_SEC, 1U, 255U);
    uint8_t balance_max_cells = GD30BM2016_LimitU8(EXTERNAL_BALANCE_MAX_CELLS, 0U, GD30BM2016_CELL_MAX);
    uint32_t vcell_mode = 0;

    if (!GD30BM2016_EnterCfgUpdate()) {
        return false;
    }

    /* Five cells are wired as VC0-VC4 and VC16, so the fifth logical cell
       is GD30 cell 16. Cell 1 is fixed enabled, bits enable cell2..cell16. */
    ok = GD30BM2016_WriteRegister(GD30_REG_VCELL_MODE, GD30_VCELL_MODE_5S_VC0_TO_VC4_VC16, 2U) && ok;
    (void)GD30BM2016_ReadRegister(GD30_REG_VCELL_MODE, &vcell_mode, 2U);
    if ((uint16_t)vcell_mode != GD30_VCELL_MODE_5S_VC0_TO_VC4_VC16) {
        GD30BM2016_ERROR("vcell mode verify fail read=0x%04X expect=0x%04X",
                         (unsigned int)vcell_mode,
                         (unsigned int)GD30_VCELL_MODE_5S_VC0_TO_VC4_VC16);
        ok = false;
    } else {
        GD30BM2016_INFO("vcell mode set 0x%04X", (unsigned int)vcell_mode);
    }

    ok = GD30BM2016_WriteRegister(GD30_REG_POWER_CONFIG, 0x4F86U, 2U) && ok;
    ok = GD30BM2016_WriteRegister(GD30_REG_REG12_CONFIG, 0x11U, 1U) && ok;
    ok = GD30BM2016_WriteRegister(GD30_REG_REG0_CONFIG, 0x01U, 1U) && ok;
    ok = GD30BM2016_WriteRegister(GD30_REG_COMM_IDLE_TIME, 0x00U, 1U) && ok;
    ok = GD30BM2016_WriteRegister(GD30_REG_DA_CONFIG, 0x05U, 1U) && ok;
    ok = GD30BM2016_WriteRegister(GD30_REG_PROTECTION_CONFIG, 0x0602U, 2U) && ok;
#if (BMS_OV_PROTECT_ENABLE != 0)
    ok = GD30BM2016_WriteRegister(GD30_REG_ENABLED_PROTECTIONS_A, 0xFCU, 1U) && ok;
#else
    ok = GD30BM2016_WriteRegister(GD30_REG_ENABLED_PROTECTIONS_A, 0xF4U, 1U) && ok;
#endif
    ok = GD30BM2016_WriteRegister(GD30_REG_ENABLED_PROTECTIONS_B,
                                  GD30_ENABLED_PROTECTIONS_B_INTERNAL_TEMP,
                                  1U) && ok;
#if (BMS_OV_PROTECT_ENABLE != 0)
    ok = GD30BM2016_WriteRegister(GD30_REG_ENABLED_PROTECTIONS_C, 0xF8U, 1U) && ok;
#else
    ok = GD30BM2016_WriteRegister(GD30_REG_ENABLED_PROTECTIONS_C, 0xE8U, 1U) && ok;
#endif
    ok = GD30BM2016_WriteRegister(GD30_REG_FET_OPTIONS, 0x0FU, 1U) && ok;
    ok = GD30BM2016_WriteRegister(GD30_REG_CHG_PUMP_CONTROL, 0x0BU, 1U) && ok;
    ok = GD30BM2016_WriteRegister(GD30_REG_MANUFACTURING, 0x50U, 1U) && ok;
    ok = GD30BM2016_WriteRegister(GD30_REG_ADC_CLK_CFG, 0x00U, 1U) && ok;

    ok = GD30BM2016_WriteRegister(GD30_REG_TS1_CONFIG, GD30_TS1_CONFIG_THERM_CELL, 1U) && ok;
    ok = GD30BM2016_WriteRegister(GD30_REG_TS23_CONFIG, GD30_TS23_CONFIG_DISABLE, 1U) && ok;
    ok = GD30BM2016_WriteRegister(GD30_REG_PAD_CFG, GD30_PAD_CFG_TS_PULLUP_18K, 1U) && ok;
    ok = GD30BM2016_WriteRegister(GD30_REG_COEFF_A1_18K, 0xEC67U, 2U) && ok;
    ok = GD30BM2016_WriteRegister(GD30_REG_COEFF_A2_18K, 0x4262U, 2U) && ok;
    ok = GD30BM2016_WriteRegister(GD30_REG_COEFF_A3_18K, 0xF6D7U, 2U) && ok;
    ok = GD30BM2016_WriteRegister(GD30_REG_COEFF_A4_18K, 0x04C4U, 2U) && ok;

    /* Three 4 mOhm shunts in parallel are 1.333 mOhm, so the GD demo's
       4 mOhm CC gain is scaled by 3. */
    ok = GD30BM2016_WriteRegister(GD30_REG_CC_GAIN, GD30BM2016_CC_GAIN, 2U) && ok;

    ok = GD30BM2016_WriteRegister(GD30_REG_CUV_THRESHOLD,
                                  GD30BM2016_MillivoltToCuvCode(config->UVPThreshold), 2U) && ok;
    ok = GD30BM2016_WriteRegister(GD30_REG_CUV_DELAY,
                                  GD30BM2016_SecondDelayCode(GD30BM2016_UvDelayToSeconds(config->UVDelay)), 2U) && ok;
    ok = GD30BM2016_WriteRegister(GD30_REG_COV_THRESHOLD,
                                  GD30BM2016_MillivoltToCovCode(config->OVPThreshold), 2U) && ok;
    ok = GD30BM2016_WriteRegister(GD30_REG_COV_DELAY,
                                  GD30BM2016_SecondDelayCode(GD30BM2016_OvDelayToSeconds(config->OVDelay)), 2U) && ok;

    ok = GD30BM2016_WriteRegister(GD30_REG_OCC_THRESHOLD,
                                  GD30BM2016_OverCurrentToRegister(config->OCCThreshold), 2U) && ok;
    ok = GD30BM2016_WriteRegister(GD30_REG_OCC_DELAY, 0xFDU, 1U) && ok;
    ok = GD30BM2016_WriteRegister(GD30_REG_OCD1_THRESHOLD,
                                  GD30BM2016_OverCurrentToRegister(config->OCDThreshold), 2U) && ok;
    ok = GD30BM2016_WriteRegister(GD30_REG_OCD1_DELAY, GD30BM2016_OcdDelayToRegister(config->OCDDelay), 1U) && ok;
    ok = GD30BM2016_WriteRegister(GD30_REG_SCD_THRESHOLD, 0x02U, 1U) && ok;
    ok = GD30BM2016_WriteRegister(GD30_REG_SCD_DELAY, GD30BM2016_ScdDelayToRegister(config->SCDDelay), 1U) && ok;

    ok = GD30BM2016_WriteRegister(GD30_REG_BALANCE_CONFIG, 0xC0U, 1U) && ok;
    ok = GD30BM2016_WriteRegister(GD30_REG_CELL_BALANCE_INTERVAL, balance_interval, 1U) && ok;
    ok = GD30BM2016_WriteRegister(GD30_REG_CELL_BALANCE_MAX_CELLS, balance_max_cells, 1U) && ok;
    ok = GD30BM2016_WriteRegister(GD30_REG_ADC_EN, 0x01U, 1U) && ok;

    ok = GD30BM2016_OnlySubcommand(GD30_SUBCMD_EXIT_CFGUPDATE) && ok;
    GD30BM2016_DELAY(50);

    return ok;
}

/**
 * @brief 轮询等待GD30完成首次ADC扫描。
 * @param 无。
 * @return true表示操作成功，false表示操作失败。
 */
static bool GD30BM2016_WaitFirstScan(void)
{
    uint16_t alarm = 0;
    uint16_t alarm_raw = 0;
    uint32_t elapsed = 0;
    const uint16_t done_mask = (uint16_t)(GD30_ALARM_ADSCAN_BIT |
                                          GD30_ALARM_FULLSCAN_BIT |
                                          GD30_ALARM_INITCOMP_BIT);

    while (elapsed < GD30_FIRST_SCAN_TIMEOUT_MS) {
        (void)GD30BM2016_ReadDirectWord(GD30_CMD_ALARM_STATUS, &alarm);
        (void)GD30BM2016_ReadDirectWord(GD30_CMD_ALARM_RAW_STATUS, &alarm_raw);

        if (((alarm | alarm_raw) & done_mask) != 0U) {
            GD30BM2016_INFO("first scan ok alarm=0x%04X raw=0x%04X",
                            (unsigned int)alarm,
                            (unsigned int)alarm_raw);
            return true;
        }

        GD30BM2016_DELAY(10);
        elapsed += 10U;
    }

    GD30BM2016_WARNING("first scan timeout alarm=0x%04X raw=0x%04X",
                       (unsigned int)alarm,
                       (unsigned int)alarm_raw);
    return false;
}

/**
 * @brief 读取并打印GD30关键状态、配置和原始测量值。
 * @param 无。
 * @return 无返回值。
 */
static void GD30BM2016_LogDiagnostics(void)
{
    uint16_t battery_status = 0;
    uint16_t alarm = 0;
    uint16_t alarm_raw = 0;
    uint16_t cell1 = 0;
    uint16_t cell2 = 0;
    uint16_t cell3 = 0;
    uint16_t cell4 = 0;
    uint16_t cell16 = 0;
    uint16_t stack = 0;
    uint16_t pack = 0;
    uint16_t current = 0;
        uint16_t manufacturing_status = 0;
uint32_t power_config = 0;
    uint32_t reg12 = 0;
    uint32_t reg0 = 0;
    uint32_t da_config = 0;
    uint32_t fet_options = 0;
    uint32_t chg_pump = 0;
    uint32_t vcell_mode = 0;
    uint32_t adc_clk = 0;
    uint32_t adc_en = 0;
    uint32_t ts1_config = 0;
    uint32_t ts23_config = 0;
    uint32_t pad_config = 0;

    (void)GD30BM2016_ReadDirectWord(GD30_CMD_BATTERY_STATUS, &battery_status);
    (void)GD30BM2016_ReadDirectWord(GD30_CMD_ALARM_STATUS, &alarm);
    (void)GD30BM2016_ReadDirectWord(GD30_CMD_ALARM_RAW_STATUS, &alarm_raw);

    (void)GD30BM2016_ReadSubcommandWord(GD30_SUBCMD_MANUFACTURING_STATUS, &manufacturing_status);
    (void)GD30BM2016_ReadRegister(GD30_REG_POWER_CONFIG, &power_config, 2U);
    (void)GD30BM2016_ReadRegister(GD30_REG_REG12_CONFIG, &reg12, 1U);
    (void)GD30BM2016_ReadRegister(GD30_REG_REG0_CONFIG, &reg0, 1U);
    (void)GD30BM2016_ReadRegister(GD30_REG_DA_CONFIG, &da_config, 1U);
    (void)GD30BM2016_ReadRegister(GD30_REG_FET_OPTIONS, &fet_options, 1U);
    (void)GD30BM2016_ReadRegister(GD30_REG_CHG_PUMP_CONTROL, &chg_pump, 1U);
    (void)GD30BM2016_ReadRegister(GD30_REG_VCELL_MODE, &vcell_mode, 2U);
    (void)GD30BM2016_ReadRegister(GD30_REG_ADC_CLK_CFG, &adc_clk, 1U);
    (void)GD30BM2016_ReadRegister(GD30_REG_ADC_EN, &adc_en, 1U);
    (void)GD30BM2016_ReadRegister(GD30_REG_TS1_CONFIG, &ts1_config, 1U);
    (void)GD30BM2016_ReadRegister(GD30_REG_TS23_CONFIG, &ts23_config, 1U);
    (void)GD30BM2016_ReadRegister(GD30_REG_PAD_CFG, &pad_config, 1U);

    (void)GD30BM2016_ReadDirectWord(GD30_CMD_CELL1_VOLTAGE, &cell1);
    (void)GD30BM2016_ReadDirectWord(GD30_CMD_CELL2_VOLTAGE, &cell2);
    (void)GD30BM2016_ReadDirectWord(GD30_CMD_CELL3_VOLTAGE, &cell3);
    (void)GD30BM2016_ReadDirectWord(GD30_CMD_CELL4_VOLTAGE, &cell4);
    (void)GD30BM2016_ReadDirectWord(GD30_CMD_CELL16_VOLTAGE, &cell16);
    (void)GD30BM2016_ReadDirectWord(GD30_CMD_STACK_VOLTAGE, &stack);
    (void)GD30BM2016_ReadDirectWord(GD30_CMD_PACK_VOLTAGE, &pack);
    (void)GD30BM2016_ReadDirectWord(GD30_CMD_CC2_CURRENT, &current);

    GD30BM2016_INFO("status bat=0x%04X alarm=0x%04X raw=0x%04X manu=0x%04X",
                    (unsigned int)battery_status,
                    (unsigned int)alarm,
                    (unsigned int)alarm_raw,
                    (unsigned int)manufacturing_status);
    GD30BM2016_INFO("cfg power=0x%04X reg12=0x%02X reg0=0x%02X da=0x%02X",
                    (unsigned int)power_config,
                    (unsigned int)reg12,
                    (unsigned int)reg0,
                    (unsigned int)da_config);
    GD30BM2016_INFO("cfg fet=0x%02X pump=0x%02X normal_cp=%u lven=%u sleep_cp=%u",
                    (unsigned int)fet_options,
                    (unsigned int)chg_pump,
                    (unsigned int)(chg_pump & 0x01U),
                    (unsigned int)((chg_pump >> 1) & 0x01U),
                    (unsigned int)((chg_pump >> 3) & 0x01U));
    GD30BM2016_INFO("cfg vcell=0x%04X adc_clk=0x%02X adc_en=0x%02X",
                    (unsigned int)vcell_mode,
                    (unsigned int)adc_clk,
                    (unsigned int)adc_en);
    GD30BM2016_INFO("cfg temp ts1=0x%02X ts23=0x%02X pad=0x%02X",
                    (unsigned int)ts1_config,
                    (unsigned int)ts23_config,
                    (unsigned int)pad_config);
    GD30BM2016_INFO("raw mV c1=%u c2=%u c3=%u c4=%u c16=%u",
                    (unsigned int)cell1,
                    (unsigned int)cell2,
                    (unsigned int)cell3,
                    (unsigned int)cell4,
                    (unsigned int)cell16);
    GD30BM2016_INFO("raw stack=%u pack=%u current=%d",
                    (unsigned int)stack,
                    (unsigned int)pack,
                    (int)(int16_t)current);
}

/**
 * @brief 读取GD30安全状态并分派外部告警回调。
 * @param GPIO_Pin 触发外部中断的GPIO引脚标识。
 * @return 无返回值。
 */
void GD30BM2016_EXTI_Callback(uint32_t GPIO_Pin)
{
    if ((GD30BM2016_ALERT_Pin == 0U) || (GPIO_Pin != GD30BM2016_ALERT_Pin)) {
        return;
    }

    uint16_t status_a = 0;
    uint16_t status_b = 0;
    uint16_t status_c = 0;

    (void)GD30BM2016_ReadDirectWord(GD30_CMD_SAFETY_STATUS_A, &status_a);
    (void)GD30BM2016_ReadDirectWord(GD30_CMD_SAFETY_STATUS_B, &status_b);
    (void)GD30BM2016_ReadDirectWord(GD30_CMD_SAFETY_STATUS_C, &status_c);

    if ((status_a & (GD30_SAFETY_A_OCD1_BIT | GD30_SAFETY_A_OCD2_BIT)) != 0U ||
        (status_c & (GD30_SAFETY_C_OCD3_BIT | GD30_SAFETY_C_OCDL_BIT)) != 0U) {
        if (AlertOps.ocd != NULL) {
            AlertOps.ocd();
        }
    }
    if ((status_a & GD30_SAFETY_A_SCD_BIT) != 0U ||
        (status_c & GD30_SAFETY_C_SCDL_BIT) != 0U) {
        if (AlertOps.scd != NULL) {
            AlertOps.scd();
        }
    }
    if ((status_a & GD30_SAFETY_A_COV_BIT) != 0U ||
        (status_c & GD30_SAFETY_C_COVL_BIT) != 0U) {
        if (AlertOps.ov != NULL) {
            AlertOps.ov();
        }
    }
    if ((status_a & GD30_SAFETY_A_CUV_BIT) != 0U) {
        if (AlertOps.uv != NULL) {
            AlertOps.uv();
        }
    }

    (void)status_b;
}

/**
 * @brief 执行地址探测、唤醒、复位和正常模式确认流程。
 * @param 无。
 * @return 无返回值。
 */
void GD30BM2016_Wakeup(void)
{
    uint16_t battery_status = 0;

    GD30BM2016_DeviceOnline = false;
    GD30BM2016_CrcMode = false;

    GD30BM2016_HoldRstShutLow();
    GD30BM2016_LogBusState("before wake");
    GD30BM2016_ClockWakeup();
    GD30BM2016_SendWakePulse(GD30BM2016_I2C_ADDR);
    GD30BM2016_SendWakePulse(0x11U);
    GD30BM2016_DELAY(350);

    if (!GD30BM2016_ProbeAddress()) {
        GD30BM2016_ScanI2cAddress();
        GD30BM2016_HardwareResetPrepare();

        if (!GD30BM2016_WaitManualTs2Address()) {
            GD30BM2016_ScanI2cAddress();
            return;
        }
    }

    if (GD30BM2016_ReadDirectWord(GD30_CMD_BATTERY_STATUS, &battery_status)) {
        GD30BM2016_INFO("battery status before cfg=0x%04X", (unsigned int)battery_status);
    } else {
        GD30BM2016_WARNING("battery status read failed before cfg");
    }

    if (!GD30BM2016_WaitNormalMode("wakeup")) {
        GD30BM2016_HardwareResetPrepare();
        GD30BM2016_ClockWakeup();
        (void)GD30BM2016_ProbeAddress();

        if (GD30BM2016_WaitManualTs2Wake()) {
            GD30BM2016_DeviceOnline = true;
            return;
        }

        GD30BM2016_ChipResetSequence();
        GD30BM2016_ClockWakeup();

        if (!GD30BM2016_ProbeAddress()) {
            GD30BM2016_ScanI2cAddress();
            return;
        }

        if (GD30BM2016_ReadDirectWord(GD30_CMD_BATTERY_STATUS, &battery_status)) {
            GD30BM2016_INFO("battery status after reset=0x%04X", (unsigned int)battery_status);
        }

        if (!GD30BM2016_WaitNormalMode("after reset")) {
            GD30BM2016_ERROR("device still sleep, keep RST_SHUT low and pull TS2 low once to wake");
            return;
        }
    }
    GD30BM2016_DeviceOnline = true;
}

/**
 * @brief 关闭FET并命令GD30进入深度休眠。
 * @param 无。
 * @return 无返回值。
 */
void GD30BM2016_EntryShip(void)
{
    (void)GD30BM2016_OnlySubcommand(GD30_SUBCMD_ALL_FETS_OFF);
    (void)GD30BM2016_OnlySubcommand(GD30_SUBCMD_SLEEP_ENABLE);
    (void)GD30BM2016_OnlySubcommand(GD30_SUBCMD_DEEPSLEEP);
    (void)GD30BM2016_OnlySubcommand(GD30_SUBCMD_DEEPSLEEP);
}

/**
 * @brief 读取GD30负载检测告警状态。
 * @param 无。
 * @return 检测到负载告警时返回 true，未检测到或读取失败时返回 false。
 */
bool GD30BM2016_LoadDetect(void)
{
    uint16_t alarm = 0;

    if (!GD30BM2016_ReadDirectWord(GD30_CMD_ALARM_STATUS, &alarm)) {
        return false;
    }

    return ((alarm & (1U << 5)) != 0U);
}

/**
 * @brief 把缓存的FET请求状态写入GD30并读取回验。
 * @param 无。
 * @return 无返回值。
 */
static void GD30BM2016_ApplyFetControl(void)
{
    uint8_t fet_off_mask = 0U;
    uint8_t fet_status = 0U;
    bool write_ok;

    if ((FetRequestState & (uint8_t)GD30_CHG_CONTROL) == 0U) {
        fet_off_mask |= GD30_FET_CONTROL_CHG_OFF;
    }
    if ((FetRequestState & (uint8_t)GD30_DSG_CONTROL) == 0U) {
        fet_off_mask |= GD30_FET_CONTROL_DSG_OFF;
    }
    if ((FetRequestState & (uint8_t)GD30_PCHG_CONTROL) == 0U) {
        fet_off_mask |= GD30_FET_CONTROL_PCHG_OFF;
    }
    if ((FetRequestState & (uint8_t)GD30_PDSG_CONTROL) == 0U) {
        fet_off_mask |= GD30_FET_CONTROL_PDSG_OFF;
    }
    write_ok = GD30BM2016_WriteRegister(GD30_SUBCMD_FET_CONTROL, fet_off_mask, 1U);
    if (GD30BM2016_ReadDirectByteQuiet(GD30_CMD_FET_STATUS, &fet_status)) {
        GD30BM2016_INFO("fet control req=0x%02X off=0x%02X status=0x%02X",
                        (unsigned int)FetRequestState,
                        (unsigned int)fet_off_mask,
                        (unsigned int)fet_status);
    } else if (write_ok == false) {
        GD30BM2016_WARNING("fet control write fail req=0x%02X off=0x%02X",
                           (unsigned int)FetRequestState,
                           (unsigned int)fet_off_mask);
    }
}
/**
 * @brief 更新指定FET通路请求并应用到GD30。
 * @param ControlType 要控制的 CHG、DSG、PCHG 或 PDSG 通路位。
 * @param NewState 目标使能状态。
 * @return 无返回值。
 */
void GD30BM2016_ControlDSGOrCHG(GD30BM2016_ControlTypedef ControlType, GD30BM2016_StateTypedef NewState)
{
    if (NewState == GD30_STATE_ENABLE) {
        FetRequestState |= (uint8_t)ControlType;
    } else {
        FetRequestState &= (uint8_t)~ControlType;
    }

    GD30BM2016_ApplyFetControl();
}

/**
 * @brief 更新指定单体的均衡请求并写入GD30。
 * @param CellIndex 目标电芯位掩码。
 * @param NewState 目标使能状态。
 * @return 无返回值。
 */
void GD30BM2016_CellBalanceControl(GD30BM2016_CellIndexTypedef CellIndex, GD30BM2016_StateTypedef NewState)
{
    uint32_t gd_mask = 0;
    uint32_t active_readback = 0;
    bool write_ok;

    if (((uint32_t)CellIndex & (uint32_t)GD30_CELL_ALL) == (uint32_t)GD30_CELL_ALL &&
        NewState == GD30_STATE_DISABLE) {
        BalanceRequestMask = 0U;
        write_ok = GD30BM2016_WriteRegister(GD30_SUBCMD_CB_ACTIVE_CELLS, 0U, 4U);
        (void)GD30BM2016_ReadRegister(GD30_SUBCMD_CB_ACTIVE_CELLS, &active_readback, 4U);
        GD30BM2016_INFO("external balance off wr=%u active=0x%08X", (unsigned int)write_ok, (unsigned int)active_readback);
        return;
    }

    for (uint8_t index = 0; index < GD30BM2016_CELL_MAX; index++) {
        if (((uint32_t)CellIndex & (1UL << index)) != 0UL) {
            gd_mask |= BalanceBitMap[index];
        }
    }

    if (gd_mask == 0UL) {
        return;
    }

    if (NewState == GD30_STATE_ENABLE) {
        BalanceRequestMask |= gd_mask;
        write_ok = GD30BM2016_WriteRegister(GD30_SUBCMD_CB_ACTIVE_CELLS, 0U, 4U);
        GD30BM2016_DELAY(EXTERNAL_BALANCE_RETRIGGER_DELAY_MS);
        write_ok = GD30BM2016_WriteRegister(GD30_SUBCMD_CB_ACTIVE_CELLS, BalanceRequestMask, 4U) && write_ok;
        GD30BM2016_DELAY(5U);
        (void)GD30BM2016_ReadRegister(GD30_SUBCMD_CB_ACTIVE_CELLS, &active_readback, 4U);
        GD30BM2016_INFO("external balance req=0x%08X gd=0x%08X wr=%u active=0x%08X",
                        (unsigned int)CellIndex,
                        (unsigned int)BalanceRequestMask,
                        (unsigned int)write_ok,
                        (unsigned int)active_readback);
    } else {
        BalanceRequestMask &= ~gd_mask;
        write_ok = GD30BM2016_WriteRegister(GD30_SUBCMD_CB_ACTIVE_CELLS, BalanceRequestMask, 4U);
        (void)GD30BM2016_ReadRegister(GD30_SUBCMD_CB_ACTIVE_CELLS, &active_readback, 4U);
        GD30BM2016_INFO("external balance update=0x%08X wr=%u active=0x%08X",
                        (unsigned int)BalanceRequestMask,
                        (unsigned int)write_ok,
                        (unsigned int)active_readback);
    }
}
/**
 * @brief 读取全部配置单体的电压并更新采样缓存。
 * @param 无。
 * @return 无返回值。
 */
void GD30BM2016_UpdateCellVolt(void)
{
    for (uint8_t index = 0; index < GD30BM2016_CELL_MAX; index++) {
        uint16_t raw = 0;

        if (GD30BM2016_ReadDirectWord(CellVoltageCommandMap[index], &raw)) {
            GD30BM2016_SampleData.CellVoltage[index] = raw / 1000.0f;
        }
    }
}

/**
 * @brief 读取外部温度通道并更新采样缓存。
 * @param 无。
 * @return 无返回值。
 */
void GD30BM2016_UpdateTsTemp(void)
{
    if (GD30BM2016_TEMP_MAX == 0U) {
        return;
    }

    for (uint8_t index = 0; index < GD30BM2016_TEMP_MAX; index++) {
        uint16_t raw = 0;

        if (GD30BM2016_ReadDirectWord(TempCommandMap[index], &raw)) {
            GD30BM2016_SampleData.TsxTemperature[index] = ((int16_t)raw) / 10.0f;
        }
    }
}

/**
 * @brief 读取GD30芯片温度并更新采样缓存。
 * @param 无。
 * @return 无返回值。
 */
void GD30BM2016_UpdateDieTemp(void)
{
    uint16_t raw = 0;

    if (GD30BM2016_ReadDirectWord(0x68U, &raw)) {
        GD30BM2016_SampleData.DieTemperature = ((int16_t)raw) / 10.0f;
    }
}

/**
 * @brief 读取CC2电流并换算为安培。
 * @param 无。
 * @return 无返回值。
 */
void GD30BM2016_UpdateCurrent(void)
{
    uint16_t raw = 0;

    if (GD30BM2016_ReadDirectWord(GD30_CMD_CC2_CURRENT, &raw)) {
        GD30BM2016_SampleData.BatteryCurrent = ((int16_t)raw) * GD30BM2016_CURRENT_USER_MA / 1000.0f;
        GD30BM2016_INFO("current raw=%d calc=%.3fA",
                        (int)(int16_t)raw,
                        GD30BM2016_SampleData.BatteryCurrent);
    }
}

/**
 * @brief 读取电池包堆栈电压并换算为伏特。
 * @param 无。
 * @return 无返回值。
 */
void GD30BM2016_UpdateBatVolt(void)
{
    uint16_t raw = 0;

    if (GD30BM2016_ReadDirectWord(GD30_CMD_STACK_VOLTAGE, &raw)) {
        GD30BM2016_SampleData.BatteryVoltage = raw * GD30BM2016_STACK_USER_MV / 1000.0f;
    }
}

/**
 * @brief 更新GD30短路保护延时。
 * @param SCDDelay 短路保护延时枚举值。
 * @return 无返回值。
 */
void GD30BM2016_SCDDelaySet(GD30BM2016_SCDDelayTypedef SCDDelay)
{
    (void)GD30BM2016_EnterCfgUpdate();
    (void)GD30BM2016_WriteRegister(GD30_REG_SCD_DELAY, GD30BM2016_ScdDelayToRegister(SCDDelay), 1U);
    (void)GD30BM2016_OnlySubcommand(GD30_SUBCMD_EXIT_CFGUPDATE);
}

/**
 * @brief 更新GD30放电过流保护延时。
 * @param OCDDelay 放电过流保护延时枚举值。
 * @return 无返回值。
 */
void GD30BM2016_OCDDelaySet(GD30BM2016_OCDDelayTypedef OCDDelay)
{
    (void)GD30BM2016_EnterCfgUpdate();
    (void)GD30BM2016_WriteRegister(GD30_REG_OCD1_DELAY, GD30BM2016_OcdDelayToRegister(OCDDelay), 1U);
    (void)GD30BM2016_OnlySubcommand(GD30_SUBCMD_EXIT_CFGUPDATE);
}

/**
 * @brief 更新GD30单体欠压保护延时。
 * @param UVDelay 单体欠压保护延时枚举值。
 * @return 无返回值。
 */
void GD30BM2016_UVDelaySet(GD30BM2016_UVDelayTypedef UVDelay)
{
    (void)GD30BM2016_EnterCfgUpdate();
    (void)GD30BM2016_WriteRegister(GD30_REG_CUV_DELAY,
                                   GD30BM2016_SecondDelayCode(GD30BM2016_UvDelayToSeconds(UVDelay)), 2U);
    (void)GD30BM2016_OnlySubcommand(GD30_SUBCMD_EXIT_CFGUPDATE);
}

/**
 * @brief 更新GD30单体过压保护延时。
 * @param OVDelay 单体过压保护延时枚举值。
 * @return 无返回值。
 */
void GD30BM2016_OVDelaySet(GD30BM2016_OVDelayTypedef OVDelay)
{
    (void)GD30BM2016_EnterCfgUpdate();
    (void)GD30BM2016_WriteRegister(GD30_REG_COV_DELAY,
                                   GD30BM2016_SecondDelayCode(GD30BM2016_OvDelayToSeconds(OVDelay)), 2U);
    (void)GD30BM2016_OnlySubcommand(GD30_SUBCMD_EXIT_CFGUPDATE);
}

/**
 * @brief 更新GD30单体欠压保护阈值。
 * @param UVPThreshold 单体欠压保护阈值，单位为毫伏。
 * @return 无返回值。
 */
void GD30BM2016_UVPThresholdSet(uint16_t UVPThreshold)
{
    (void)GD30BM2016_EnterCfgUpdate();
    (void)GD30BM2016_WriteRegister(GD30_REG_CUV_THRESHOLD, GD30BM2016_MillivoltToCuvCode(UVPThreshold), 2U);
    (void)GD30BM2016_OnlySubcommand(GD30_SUBCMD_EXIT_CFGUPDATE);
}

/**
 * @brief 更新GD30单体过压保护阈值。
 * @param OVPThreshold 单体过压保护阈值，单位为毫伏。
 * @return 无返回值。
 */
void GD30BM2016_OVPThresholdSet(uint16_t OVPThreshold)
{
    (void)GD30BM2016_EnterCfgUpdate();
    (void)GD30BM2016_WriteRegister(GD30_REG_COV_THRESHOLD, GD30BM2016_MillivoltToCovCode(OVPThreshold), 2U);
    (void)GD30BM2016_OnlySubcommand(GD30_SUBCMD_EXIT_CFGUPDATE);
}

/**
 * @brief 初始化GD30驱动、应用配置并确认首次采样。
 * @param InitData 初始化配置及 AFE 告警回调集合。
 * @return true表示操作成功，false表示操作失败。
 */
bool GD30BM2016_Initialize(GD30BM2016_InitDataTypedef *InitData)
{
    if (InitData == NULL) {
        return false;
    }

    AlertOps = InitData->AlertOps;
    FetRequestState = 0U;
    BalanceRequestMask = 0U;

    GD30BM2016_Wakeup();

    if (!GD30BM2016_DeviceOnline) {
        GD30BM2016_ERROR("device not ready, check RST_SHUT low, TS2 wake button and cell power");
        return false;
    }

    if (!GD30BM2016_ApplyConfig(&InitData->ConfigData)) {
        GD30BM2016_ERROR("config fail");
        return false;
    }
    GD30BM2016_ApplyFetControl();
    (void)GD30BM2016_WaitFirstScan();
    GD30BM2016_LogDiagnostics();
    GD30BM2016_INFO("initialize successful");
    return true;
}
