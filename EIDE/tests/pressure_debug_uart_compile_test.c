#include "..\..\Core\Inc\pressure_debug_uart.h"

typedef char test_debug_text_default_is_disabled[
    (PRESSURE_UART_DEBUG_TEXT_ENABLE == 0U) ? 1 : -1];
typedef char test_debug_text_buffer_is_large_enough[
    (PRESSURE_UART_DEBUG_TEXT_BUF_SIZE >= 24U) ? 1 : -1];

void pressure_debug_uart_compile_test(void)
{
    char text[PRESSURE_UART_DEBUG_TEXT_BUF_SIZE];
    uint16_t written = PressureDebugUart_FormatSampleLine(text,
                                                          sizeof(text),
                                                          -12345,
                                                          678U);

    (void)written;
}
