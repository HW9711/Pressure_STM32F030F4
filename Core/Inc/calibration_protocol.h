#ifndef __CALIBRATION_PROTOCOL_H__
#define __CALIBRATION_PROTOCOL_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"
#include "usart.h"

/* 上位机到 MCU 的标定命令使用 A5 5A 帧头，避免和 AA55 周期上报帧混淆。 */
#define CAL_PROTOCOL_HEADER0              0xA5U
#define CAL_PROTOCOL_HEADER1              0x5AU
#define CAL_PROTOCOL_TAIL0                0x5AU
#define CAL_PROTOCOL_TAIL1                0xA5U
#define CAL_PROTOCOL_VERSION              0x01U
#define CAL_PROTOCOL_CMD_PING             0x01U
#define CAL_PROTOCOL_CMD_SET_TABLE        0x10U
#define CAL_PROTOCOL_CMD_CLEAR_TABLE      0x11U
#define CAL_PROTOCOL_CMD_SET_THRESHOLD    0x12U
#define CAL_PROTOCOL_RSP_BASE             0x80U
#define CAL_PROTOCOL_STATUS_OK            0x00U
#define CAL_PROTOCOL_STATUS_BAD_FORMAT    0x01U
#define CAL_PROTOCOL_STATUS_BAD_LENGTH    0x02U
#define CAL_PROTOCOL_STATUS_BAD_TABLE     0x03U
#define CAL_PROTOCOL_STATUS_FLASH_FAIL    0x04U
#define CAL_PROTOCOL_STATUS_UNKNOWN_CMD   0x05U
#define CAL_PROTOCOL_STATUS_BAD_VALUE     0x06U

void CalibrationProtocol_Init(UART_HandleTypeDef *huart);
void CalibrationProtocol_Process(UART_HandleTypeDef *huart);
void CalibrationProtocol_OnRxByte(uint8_t byte);

#ifdef __cplusplus
}
#endif

#endif /* __CALIBRATION_PROTOCOL_H__ */
