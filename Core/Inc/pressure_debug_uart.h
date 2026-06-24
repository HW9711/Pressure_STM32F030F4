#ifndef __PRESSURE_DEBUG_UART_H__
#define __PRESSURE_DEBUG_UART_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

/* 临时调试开关：1 输出可读字符串，0 保持现有二进制协议 */
#ifndef PRESSURE_UART_DEBUG_TEXT_ENABLE
#define PRESSURE_UART_DEBUG_TEXT_ENABLE      0U
#endif

/* 调试字符串缓冲区长度 */
#define PRESSURE_UART_DEBUG_TEXT_BUF_SIZE    48U

static __inline uint16_t PressureDebugUart_FormatSampleLine(char *buffer,
                                                            size_t buffer_size,
                                                            int32_t raw_value,
                                                            uint32_t calibrated_value_x10)
{
    int written;
    unsigned long calibrated_int;
    unsigned long calibrated_frac;

    if ((buffer == NULL) || (buffer_size == 0U)) {
        return 0U;
    }

    calibrated_int = (unsigned long)(calibrated_value_x10 / 10U);
    calibrated_frac = (unsigned long)(calibrated_value_x10 % 10U);
    written = snprintf(buffer,
                       buffer_size,
                       "raw=%ld cal=%lu.%01lug\r\n",
                       (long)raw_value,
                       calibrated_int,
                       calibrated_frac);

    if (written < 0) {
        buffer[0] = '\0';
        return 0U;
    }

    if ((size_t)written >= buffer_size) {
        return (uint16_t)(buffer_size - 1U);
    }

    return (uint16_t)written;
}

#ifdef __cplusplus
}
#endif

#endif /* __PRESSURE_DEBUG_UART_H__ */
