#include "protocol.h"
#include "..\..\Core\Src\protocol.c"

typedef char test_frame_size_is_21[
    (CS1237_UART_PROTOCOL_FRAME_SIZE == 21U) ? 1 : -1];
typedef char test_payload_len_is_12[
    (CS1237_UART_PROTOCOL_PAYLOAD_LEN == 12U) ? 1 : -1];
typedef char test_crc_len_is_15[
    (CS1237_UART_PROTOCOL_CRC_DATA_LEN == 15U) ? 1 : -1];
typedef char test_header_bytes_are_fixed[
    (CS1237_UART_PROTOCOL_HEADER0 == 0xAAU &&
     CS1237_UART_PROTOCOL_HEADER1 == 0x55U) ? 1 : -1];
typedef char test_tail_bytes_are_fixed[
    (CS1237_UART_PROTOCOL_TAIL0 == 0x55U &&
     CS1237_UART_PROTOCOL_TAIL1 == 0xAAU) ? 1 : -1];
typedef char test_protocol_version_is_02[
    (CS1237_UART_PROTOCOL_VERSION == 0x02U) ? 1 : -1];
typedef char test_offsets_match_wire_format[
    (CS1237_UART_PROTOCOL_OFFSET_PROTOCOL_VER == 2U &&
     CS1237_UART_PROTOCOL_OFFSET_MSG_TYPE == 3U &&
     CS1237_UART_PROTOCOL_OFFSET_PAYLOAD_LEN == 4U &&
     CS1237_UART_PROTOCOL_OFFSET_SEQ == 5U &&
     CS1237_UART_PROTOCOL_OFFSET_RAW_CS1237 == 6U &&
     CS1237_UART_PROTOCOL_OFFSET_WEIGHT_X10 == 10U &&
     CS1237_UART_PROTOCOL_OFFSET_THRESHOLD_G == 14U &&
     CS1237_UART_PROTOCOL_OFFSET_DEVICE_CODE == 16U &&
     CS1237_UART_PROTOCOL_OFFSET_CRC16 == 17U &&
     CS1237_UART_PROTOCOL_OFFSET_TAIL0 == 19U &&
     CS1237_UART_PROTOCOL_OFFSET_TAIL1 == 20U) ? 1 : -1];

void protocol_compile_test(void)
{
    uint8_t frame[CS1237_UART_PROTOCOL_FRAME_SIZE];
    uint16_t written =
        CS1237UartProtocol_BuildReportFrame(frame,
                                            CS1237_UART_PROTOCOL_FRAME_SIZE,
                                            0xFFU,
                                            -123456,
                                            3210U,
                                            350U,
                                            CS1237_UART_PROTOCOL_DEVICE_CODE_1111);

    (void)written;
    (void)CS1237UartProtocol_PackDeviceCode(1U, 0U, 0U, 1U);
    (void)CS1237UartProtocol_IsKnownDeviceCode(frame[CS1237_UART_PROTOCOL_OFFSET_DEVICE_CODE]);
    (void)CS1237UartProtocol_Crc16Modbus(frame + CS1237_UART_PROTOCOL_OFFSET_PROTOCOL_VER,
                                         CS1237_UART_PROTOCOL_CRC_DATA_LEN);
}
