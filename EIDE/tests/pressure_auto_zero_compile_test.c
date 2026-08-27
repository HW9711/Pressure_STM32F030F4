#include "cs1237.h"
#include "pressure_auto_zero.h"
#include "pressure_threshold_store.h"
#include "pressure_zero_store.h"
#include "protocol.h"
#include "weight_calibration.h"

typedef char test_zero_window_uses_25_conversions[
    ((PRESSURE_AUTO_ZERO_WINDOW_SIZE * CS1237_MEDIAN_LEN) == 25U) ? 1 : -1];
typedef char test_failsafe_exceeds_main_stop_limit[
    (PRESSURE_AUTO_ZERO_FAILSAFE_X10 > 5000U) ? 1 : -1];
typedef char test_preload_limit_is_50g[
    (PRESSURE_AUTO_ZERO_PRELOAD_MAX_X10 == 500U) ? 1 : -1];
typedef char test_small_negative_preload_is_tolerated[
    (PRESSURE_AUTO_ZERO_PRELOAD_NEG_TOL_RAW == -1500) ? 1 : -1];
typedef char test_hard_limit_is_100g[
    (PRESSURE_AUTO_ZERO_PRELOAD_HARD_X10 == 1000U) ? 1 : -1];
typedef char test_factory_and_threshold_pages_do_not_overlap[
    ((PRESSURE_ZERO_FLASH_ADDR + 0x400U) <= PRESSURE_THRESHOLD_FLASH_ADDR) ? 1 : -1];
typedef char test_report_frame_size_is_unchanged[
    (CS1237_UART_PROTOCOL_FRAME_SIZE == 21U) ? 1 : -1];

void pressure_auto_zero_compile_test(void)
{
  int32_t raw_value = 0;

  (void)CS1237_ReadMedianChecked(0U, &raw_value);
  PressureAutoZero_Init();
  PressureAutoZero_Update(raw_value,
                          1U,
                          CS1237_UART_PROTOCOL_DEVICE_CODE_INJECT_WATER);
  (void)PressureAutoZero_IsReady();
  (void)PressureAutoZero_GetState();
  (void)PressureAutoZero_GetFault();
  (void)WeightCalibration_ApplyFromEmptyX10(raw_value);
  (void)WeightCalibration_ApplySessionX10(raw_value, 0);
}
