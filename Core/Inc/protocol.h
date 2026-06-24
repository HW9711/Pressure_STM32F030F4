#ifndef __PROTOCOL_H__
#define __PROTOCOL_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"

/* 固定帧定界符：用于主控在串口字节流中快速同步帧边界 */
#define CS1237_UART_PROTOCOL_HEADER0            0xAAU
#define CS1237_UART_PROTOCOL_HEADER1            0x55U
#define CS1237_UART_PROTOCOL_TAIL0              0x55U
#define CS1237_UART_PROTOCOL_TAIL1              0xAAU

/* 协议版本 0x02：相比 v1 新增了压力阈值字段 ThresholdG */
#define CS1237_UART_PROTOCOL_VERSION            0x02U

/* 0x01 表示周期主动上报的数据帧 */
#define CS1237_UART_PROTOCOL_MSG_TYPE_REPORT    0x01U

/* PayloadLen 只统计 Seq 到 DeviceCode 之间的业务数据长度 */
#define CS1237_UART_PROTOCOL_PAYLOAD_LEN        0x0CU

/* CRC 覆盖范围是 ProtocolVer 到 DeviceCode，共 15 字节 */
#define CS1237_UART_PROTOCOL_CRC_DATA_LEN       0x0FU

/* 整帧总长度：帧头 2 + 版本/类型/长度 3 + Payload 12 + CRC 2 + 帧尾 2 */
#define CS1237_UART_PROTOCOL_FRAME_SIZE         21U

/* 各字段在整帧中的固定偏移，主控可直接按这些位置解析 */
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

/* 当前约定的合法设备类型码，对应 PA1~PA4 的霍尔稳定状态 */
#define CS1237_UART_PROTOCOL_DEVICE_CODE_1111   0x0FU
#define CS1237_UART_PROTOCOL_DEVICE_CODE_1110   0x0EU
#define CS1237_UART_PROTOCOL_DEVICE_CODE_1100   0x0CU
#define CS1237_UART_PROTOCOL_DEVICE_CODE_1000   0x08U
#define CS1237_UART_PROTOCOL_DEVICE_CODE_1001   0x09U

/* 把 4 路霍尔稳定状态打包为设备码：PA1..PA4 对应 bit3..bit0 */
uint8_t CS1237UartProtocol_PackDeviceCode(uint8_t st1,
                                          uint8_t st2,
                                          uint8_t st3,
                                          uint8_t st4);

/* 判断设备码是否属于当前定义的 5 种合法组合 */
uint8_t CS1237UartProtocol_IsKnownDeviceCode(uint8_t device_code);

/* 计算 CRC16/MODBUS，初值 0xFFFF，多项式 0xA001 */
uint16_t CS1237UartProtocol_Crc16Modbus(const uint8_t *data, uint16_t len);

/* 组装完整上报帧。返回值为实际帧长；参数非法时返回 0 */
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
