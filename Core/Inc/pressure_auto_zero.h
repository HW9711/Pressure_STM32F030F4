#ifndef __PRESSURE_AUTO_ZERO_H__
#define __PRESSURE_AUTO_ZERO_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"

/* Invalid/not-ready output is above every current main-controller stop limit. */
#define PRESSURE_AUTO_ZERO_FAILSAFE_X10             6000U

/* Five median frames contain 25 direct CS1237 conversions. */
#define PRESSURE_AUTO_ZERO_WINDOW_SIZE              5U
#define PRESSURE_AUTO_ZERO_STABLE_SPAN_RAW          1500
#define PRESSURE_AUTO_ZERO_STABLE_STDDEV_RAW        550
#define PRESSURE_AUTO_ZERO_STABLE_DRIFT_RAW         500

/* The empty tube may add almost no load; reject only excessive or negative offset. */
#define PRESSURE_AUTO_ZERO_FACTORY_ABS_MAX_RAW      100000
#define PRESSURE_AUTO_ZERO_FACTORY_DRIFT_MAX_X10    500U
#define PRESSURE_AUTO_ZERO_PRELOAD_NEG_TOL_RAW      (-1500)
#define PRESSURE_AUTO_ZERO_PRELOAD_MAX_X10          500U
#define PRESSURE_AUTO_ZERO_PRELOAD_HARD_X10         1000U
#define PRESSURE_AUTO_ZERO_NEGATIVE_FAULT_X10       200U
#define PRESSURE_AUTO_ZERO_DEVICE_STABLE_FRAMES     2U
#define PRESSURE_AUTO_ZERO_INVALID_LIMIT            3U
#define PRESSURE_AUTO_ZERO_NEGATIVE_LIMIT           3U

typedef enum {
  PRESSURE_AUTO_ZERO_WAIT_FACTORY_EMPTY = 0,
  PRESSURE_AUTO_ZERO_WAIT_BOOT_EMPTY,
  PRESSURE_AUTO_ZERO_WAIT_DEVICE,
  PRESSURE_AUTO_ZERO_CAPTURE_PRELOAD,
  PRESSURE_AUTO_ZERO_READY,
  PRESSURE_AUTO_ZERO_WAIT_EMPTY_RECOVERY,
  PRESSURE_AUTO_ZERO_LOCKOUT
} PressureAutoZeroState_t;

typedef enum {
  PRESSURE_AUTO_ZERO_FAULT_NONE = 0,
  PRESSURE_AUTO_ZERO_FAULT_SAMPLE,
  PRESSURE_AUTO_ZERO_FAULT_BOOT_LOADED,
  PRESSURE_AUTO_ZERO_FAULT_FACTORY_RANGE,
  PRESSURE_AUTO_ZERO_FAULT_FACTORY_DRIFT,
  PRESSURE_AUTO_ZERO_FAULT_FLASH,
  PRESSURE_AUTO_ZERO_FAULT_PRELOAD_LOW,
  PRESSURE_AUTO_ZERO_FAULT_PRELOAD_HIGH,
  PRESSURE_AUTO_ZERO_FAULT_PRELOAD_HARD,
  PRESSURE_AUTO_ZERO_FAULT_DEVICE_CHANGED,
  PRESSURE_AUTO_ZERO_FAULT_ZERO_LOST
} PressureAutoZeroFault_t;

void PressureAutoZero_Init(void);
void PressureAutoZero_Update(int32_t raw_value,
                             uint8_t sample_valid,
                             uint8_t device_code);
uint8_t PressureAutoZero_IsReady(void);
int32_t PressureAutoZero_GetFactoryEmptyRaw(void);
int32_t PressureAutoZero_GetBootEmptyRaw(void);
int32_t PressureAutoZero_GetSessionZeroRaw(void);
PressureAutoZeroState_t PressureAutoZero_GetState(void);
PressureAutoZeroFault_t PressureAutoZero_GetFault(void);

#ifdef __cplusplus
}
#endif

#endif /* __PRESSURE_AUTO_ZERO_H__ */
