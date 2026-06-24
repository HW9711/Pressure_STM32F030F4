#include "protocol.h"

/* 以小端格式写入 16 位数据，和协议中的 CRC/阈值字段保持一致 */
static void CS1237UartProtocol_WriteU16LittleEndian(uint8_t *dst, uint16_t value)
{
    dst[0] = (uint8_t)(value & 0xFFU);
    dst[1] = (uint8_t)((value >> 8) & 0xFFU);
}

/* 以小端格式写入 32 位数据，供原始值和重量字段复用 */
static void CS1237UartProtocol_WriteU32LittleEndian(uint8_t *dst, uint32_t value)
{
    dst[0] = (uint8_t)(value & 0xFFU);
    dst[1] = (uint8_t)((value >> 8) & 0xFFU);
    dst[2] = (uint8_t)((value >> 16) & 0xFFU);
    dst[3] = (uint8_t)((value >> 24) & 0xFFU);
}

uint8_t CS1237UartProtocol_PackDeviceCode(uint8_t st1,
                                          uint8_t st2,
                                          uint8_t st3,
                                          uint8_t st4)
{
    /* 协议约定：PA1..PA4 依次映射到 bit3..bit0 */
    return (uint8_t)(((st1 & 0x01U) << 3) |
                     ((st2 & 0x01U) << 2) |
                     ((st3 & 0x01U) << 1) |
                     (st4 & 0x01U));
}

uint8_t CS1237UartProtocol_IsKnownDeviceCode(uint8_t device_code)
{
    /* 只校验低 4 位，高位即使被污染也不影响设备码判定 */
    switch (device_code & 0x0FU) {
    case CS1237_UART_PROTOCOL_DEVICE_CODE_1111:
    case CS1237_UART_PROTOCOL_DEVICE_CODE_1110:
    case CS1237_UART_PROTOCOL_DEVICE_CODE_1100:
    case CS1237_UART_PROTOCOL_DEVICE_CODE_1000:
    case CS1237_UART_PROTOCOL_DEVICE_CODE_1001:
        return 1U;
    default:
        return 0U;
    }
}

uint16_t CS1237UartProtocol_Crc16Modbus(const uint8_t *data, uint16_t len)
{
    uint16_t crc = 0xFFFFU;
    uint16_t i;
    uint8_t bit;

    /* 空指针直接返回 0，避免上层误用时访问非法地址 */
    if (data == NULL) {
        return 0U;
    }

    /* 标准 CRC16/MODBUS 位移实现，便于在 MCU 端保持可读性 */
    for (i = 0U; i < len; i++) {
        crc ^= data[i];
        for (bit = 0U; bit < 8U; bit++) {
            if ((crc & 0x0001U) != 0U) {
                crc = (uint16_t)((crc >> 1) ^ 0xA001U);
            } else {
                crc >>= 1;
            }
        }
    }

    return crc;
}

uint16_t CS1237UartProtocol_BuildReportFrame(uint8_t *frame,
                                             uint16_t frame_size,
                                             uint8_t seq,
                                             int32_t raw_cs1237,
                                             uint32_t weight_x10,
                                             uint16_t threshold_g,
                                             uint8_t device_code)
{
    uint16_t crc;

    /* 缓冲区不存在，或长度不足以容纳整帧时，直接判定失败 */
    if ((frame == NULL) || (frame_size < CS1237_UART_PROTOCOL_FRAME_SIZE)) {
        return 0U;
    }

    /* 先按固定偏移写入协议头和基础控制字段 */
    frame[CS1237_UART_PROTOCOL_OFFSET_HEADER0] = CS1237_UART_PROTOCOL_HEADER0;
    frame[CS1237_UART_PROTOCOL_OFFSET_HEADER1] = CS1237_UART_PROTOCOL_HEADER1;
    frame[CS1237_UART_PROTOCOL_OFFSET_PROTOCOL_VER] = CS1237_UART_PROTOCOL_VERSION;
    frame[CS1237_UART_PROTOCOL_OFFSET_MSG_TYPE] = CS1237_UART_PROTOCOL_MSG_TYPE_REPORT;
    frame[CS1237_UART_PROTOCOL_OFFSET_PAYLOAD_LEN] = CS1237_UART_PROTOCOL_PAYLOAD_LEN;
    frame[CS1237_UART_PROTOCOL_OFFSET_SEQ] = seq;

    /* 依次写入业务字段：原始值、最终重量、压力阈值、设备类型码 */
    CS1237UartProtocol_WriteU32LittleEndian(
        &frame[CS1237_UART_PROTOCOL_OFFSET_RAW_CS1237],
        (uint32_t)raw_cs1237);
    CS1237UartProtocol_WriteU32LittleEndian(
        &frame[CS1237_UART_PROTOCOL_OFFSET_WEIGHT_X10],
        weight_x10);
    CS1237UartProtocol_WriteU16LittleEndian(
        &frame[CS1237_UART_PROTOCOL_OFFSET_THRESHOLD_G],
        threshold_g);
    frame[CS1237_UART_PROTOCOL_OFFSET_DEVICE_CODE] = (uint8_t)(device_code & 0x0FU);

    /* CRC 不包含帧头、CRC 自身和帧尾，只覆盖协议规定的中间区间 */
    crc = CS1237UartProtocol_Crc16Modbus(
        &frame[CS1237_UART_PROTOCOL_OFFSET_PROTOCOL_VER],
        CS1237_UART_PROTOCOL_CRC_DATA_LEN);
    CS1237UartProtocol_WriteU16LittleEndian(
        &frame[CS1237_UART_PROTOCOL_OFFSET_CRC16],
        crc);

    frame[CS1237_UART_PROTOCOL_OFFSET_TAIL0] = CS1237_UART_PROTOCOL_TAIL0;
    frame[CS1237_UART_PROTOCOL_OFFSET_TAIL1] = CS1237_UART_PROTOCOL_TAIL1;

    /* 返回固定帧长，调用者可直接拿这个长度去发送 UART */
    return CS1237_UART_PROTOCOL_FRAME_SIZE;
}
