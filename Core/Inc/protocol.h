#ifndef __PROTOCOL_H__
#define __PROTOCOL_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"

#define CS1237_UART_PROTOCOL_HEADER0            0xAAU
#define CS1237_UART_PROTOCOL_HEADER1            0x55U
#define CS1237_UART_PROTOCOL_TAIL0              0x55U
#define CS1237_UART_PROTOCOL_TAIL1              0xAAU
#define CS1237_UART_PROTOCOL_VERSION            0x02U
#define CS1237_UART_PROTOCOL_MSG_TYPE_REPORT    0x01U
#define CS1237_UART_PROTOCOL_PAYLOAD_LEN        0x0CU
#define CS1237_UART_PROTOCOL_CRC_DATA_LEN       0x0FU
#define CS1237_UART_PROTOCOL_FRAME_SIZE         21U

#define CS1237_UART_PROTOCOL_OFFSET_HEADER0         0U
#define CS1237_UART_PROTOCOL_OFFSET_HEADER1         1U
#define CS1237_UART_PROTOCOL_OFFSET_PROTOCOL_VER    2U
#define CS1237_UART_PROTOCOL_OFFSET_MSG_TYPE        3U
#define CS1237_UART_PROTOCOL_OFFSET_PAYLOAD_LEN     4U
#define CS1237_UART_PROTOCOL_OFFSET_SEQ             5U
#define CS1237_UART_PROTOCOL_OFFSET_RAW_CS1237      6U
#define CS1237_UART_PROTOCOL_OFFSET_WEIGHT_X10      10U
#define CS1237_UART_PROTOCOL_OFFSET_THRESHOLD_G     14U
#define CS1237_UART_PROTOCOL_OFFSET_DEVICE_CODE     16U
#define CS1237_UART_PROTOCOL_OFFSET_CRC16           17U
#define CS1237_UART_PROTOCOL_OFFSET_TAIL0           19U
#define CS1237_UART_PROTOCOL_OFFSET_TAIL1           20U

#define CS1237_UART_PROTOCOL_DEVICE_CODE_1111   0x0FU
#define CS1237_UART_PROTOCOL_DEVICE_CODE_1110   0x0EU
#define CS1237_UART_PROTOCOL_DEVICE_CODE_1100   0x0CU
#define CS1237_UART_PROTOCOL_DEVICE_CODE_1000   0x08U
#define CS1237_UART_PROTOCOL_DEVICE_CODE_1001   0x09U

uint8_t CS1237UartProtocol_PackDeviceCode(uint8_t st1,
                                          uint8_t st2,
                                          uint8_t st3,
                                          uint8_t st4);
uint8_t CS1237UartProtocol_IsKnownDeviceCode(uint8_t device_code);
uint16_t CS1237UartProtocol_Crc16Modbus(const uint8_t *data, uint16_t len);
uint16_t CS1237UartProtocol_BuildReportFrame(uint8_t *frame,
                                             uint16_t frame_size,
                                             uint8_t seq,
                                             int32_t raw_cs1237,
                                             uint32_t weight_x10,
                                             uint16_t threshold_g,
                                             uint8_t device_code);

#ifdef __cplusplus
}
#endif

#endif /* __PROTOCOL_H__ */
