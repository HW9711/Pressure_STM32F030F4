#include "pressure_auto_zero.h"

#include "pressure_zero_store.h"
#include "protocol.h"
#include "weight_calibration.h"

static PressureAutoZeroState_t g_state = PRESSURE_AUTO_ZERO_WAIT_FACTORY_EMPTY;
static PressureAutoZeroFault_t g_fault = PRESSURE_AUTO_ZERO_FAULT_NONE;
static int32_t g_factory_empty_raw = 0;
static int32_t g_boot_empty_raw = 0;
static int32_t g_session_zero_raw = 0;
static int32_t g_window[PRESSURE_AUTO_ZERO_WINDOW_SIZE];
static uint8_t g_window_count = 0U;
static uint8_t g_factory_valid = 0U;
static uint8_t g_last_device_code = 0U;
static uint8_t g_device_stable_count = 0U;
static uint8_t g_invalid_count = 0U;
static uint8_t g_negative_count = 0U;

static int32_t PressureAutoZero_AbsDiff(int32_t a, int32_t b)
{
  int64_t diff = (int64_t)a - (int64_t)b;

  if (diff < 0LL) {
    diff = -diff;
  }
  if (diff > 0x7FFFFFFFLL) {
    return 0x7FFFFFFF;
  }
  return (int32_t)diff;
}

static void PressureAutoZero_ResetWindow(void)
{
  g_window_count = 0U;
}

static void PressureAutoZero_ResetDevice(void)
{
  g_last_device_code = 0U;
  g_device_stable_count = 0U;
}

static uint8_t PressureAutoZero_AddStableSample(int32_t raw_value, int32_t *mean_raw)
{
  int32_t min_raw;
  int32_t max_raw;
  int64_t sum = 0LL;
  int64_t variance_sum = 0LL;
  int64_t first_sum = 0LL;
  int64_t last_sum = 0LL;
  int32_t mean;
  int32_t first_mean;
  int32_t last_mean;
  uint8_t first_count = PRESSURE_AUTO_ZERO_WINDOW_SIZE / 2U;
  uint8_t last_start = (uint8_t)(PRESSURE_AUTO_ZERO_WINDOW_SIZE - first_count);
  uint8_t i;

  if ((mean_raw == NULL) || (g_window_count >= PRESSURE_AUTO_ZERO_WINDOW_SIZE)) {
    PressureAutoZero_ResetWindow();
    return 0U;
  }

  g_window[g_window_count] = raw_value;
  g_window_count++;
  if (g_window_count < PRESSURE_AUTO_ZERO_WINDOW_SIZE) {
    return 0U;
  }

  min_raw = g_window[0];
  max_raw = g_window[0];
  for (i = 0U; i < PRESSURE_AUTO_ZERO_WINDOW_SIZE; i++) {
    if (g_window[i] < min_raw) {
      min_raw = g_window[i];
    }
    if (g_window[i] > max_raw) {
      max_raw = g_window[i];
    }
    sum += g_window[i];
  }
  mean = (int32_t)(sum / PRESSURE_AUTO_ZERO_WINDOW_SIZE);

  for (i = 0U; i < PRESSURE_AUTO_ZERO_WINDOW_SIZE; i++) {
    int64_t diff = (int64_t)g_window[i] - (int64_t)mean;
    variance_sum += diff * diff;
  }
  for (i = 0U; i < first_count; i++) {
    first_sum += g_window[i];
    last_sum += g_window[last_start + i];
  }
  first_mean = (int32_t)(first_sum / first_count);
  last_mean = (int32_t)(last_sum / first_count);

  PressureAutoZero_ResetWindow();
  if (((int64_t)max_raw - (int64_t)min_raw) > PRESSURE_AUTO_ZERO_STABLE_SPAN_RAW) {
    return 0U;
  }
  if (variance_sum > ((int64_t)PRESSURE_AUTO_ZERO_WINDOW_SIZE *
                      PRESSURE_AUTO_ZERO_STABLE_STDDEV_RAW *
                      PRESSURE_AUTO_ZERO_STABLE_STDDEV_RAW)) {
    return 0U;
  }
  if (PressureAutoZero_AbsDiff(first_mean, last_mean) > PRESSURE_AUTO_ZERO_STABLE_DRIFT_RAW) {
    return 0U;
  }

  *mean_raw = mean;
  return 1U;
}

static uint8_t PressureAutoZero_FactoryRawIsPlausible(int32_t raw_value)
{
  return ((raw_value >= -PRESSURE_AUTO_ZERO_FACTORY_ABS_MAX_RAW) &&
          (raw_value <= PRESSURE_AUTO_ZERO_FACTORY_ABS_MAX_RAW)) ? 1U : 0U;
}

static uint8_t PressureAutoZero_EmptyMatchesFactory(int32_t raw_value)
{
  int32_t drift_raw;

  if ((g_factory_valid == 0U) ||
      (PressureAutoZero_FactoryRawIsPlausible(raw_value) == 0U)) {
    return 0U;
  }
  drift_raw = PressureAutoZero_AbsDiff(raw_value, g_factory_empty_raw);
  return (WeightCalibration_ApplyFromEmptyX10(drift_raw) <=
          PRESSURE_AUTO_ZERO_FACTORY_DRIFT_MAX_X10) ? 1U : 0U;
}

static void PressureAutoZero_EnterLockout(PressureAutoZeroFault_t fault)
{
  g_state = PRESSURE_AUTO_ZERO_LOCKOUT;
  g_fault = fault;
  g_invalid_count = 0U;
  g_negative_count = 0U;
  PressureAutoZero_ResetWindow();
  PressureAutoZero_ResetDevice();
}

static void PressureAutoZero_AcceptEmpty(int32_t mean_raw)
{
  g_boot_empty_raw = mean_raw;
  g_state = PRESSURE_AUTO_ZERO_WAIT_DEVICE;
  g_fault = PRESSURE_AUTO_ZERO_FAULT_NONE;
  g_invalid_count = 0U;
  g_negative_count = 0U;
  PressureAutoZero_ResetWindow();
  PressureAutoZero_ResetDevice();
}

static void PressureAutoZero_ProcessEmpty(int32_t raw_value, uint8_t save_factory)
{
  int32_t mean_raw;

  if (PressureAutoZero_AddStableSample(raw_value, &mean_raw) == 0U) {
    return;
  }

  if (save_factory != 0U) {
    if (PressureAutoZero_FactoryRawIsPlausible(mean_raw) == 0U) {
      PressureAutoZero_EnterLockout(PRESSURE_AUTO_ZERO_FAULT_FACTORY_RANGE);
      return;
    }
    if (PressureZeroStore_Save(mean_raw) == 0U) {
      PressureAutoZero_EnterLockout(PRESSURE_AUTO_ZERO_FAULT_FLASH);
      return;
    }
    g_factory_empty_raw = mean_raw;
    g_factory_valid = 1U;
    PressureAutoZero_AcceptEmpty(mean_raw);
    return;
  }

  if (PressureAutoZero_EmptyMatchesFactory(mean_raw) == 0U) {
    PressureAutoZero_EnterLockout(PRESSURE_AUTO_ZERO_FAULT_FACTORY_DRIFT);
    return;
  }
  PressureAutoZero_AcceptEmpty(mean_raw);
}

void PressureAutoZero_Init(void)
{
  g_factory_empty_raw = 0;
  g_boot_empty_raw = 0;
  g_session_zero_raw = 0;
  g_fault = PRESSURE_AUTO_ZERO_FAULT_NONE;
  g_invalid_count = 0U;
  g_negative_count = 0U;
  PressureAutoZero_ResetWindow();
  PressureAutoZero_ResetDevice();

  g_factory_valid = PressureZeroStore_Load(&g_factory_empty_raw);
  g_state = (g_factory_valid != 0U) ?
      PRESSURE_AUTO_ZERO_WAIT_BOOT_EMPTY :
      PRESSURE_AUTO_ZERO_WAIT_FACTORY_EMPTY;
}

void PressureAutoZero_Update(int32_t raw_value,
                             uint8_t sample_valid,
                             uint8_t device_code)
{
  uint8_t known_device = CS1237UartProtocol_IsKnownDeviceCode(device_code);
  int32_t mean_raw;
  int64_t preload_delta;
  int64_t pressure_delta;
  uint32_t preload_x10;

  if (sample_valid == 0U) {
    PressureAutoZero_ResetWindow();
    if (g_state != PRESSURE_AUTO_ZERO_READY) {
      PressureAutoZero_ResetDevice();
    }
    if (g_invalid_count < PRESSURE_AUTO_ZERO_INVALID_LIMIT) {
      g_invalid_count++;
    }
    if ((g_state == PRESSURE_AUTO_ZERO_READY) &&
        (g_invalid_count >= PRESSURE_AUTO_ZERO_INVALID_LIMIT)) {
      PressureAutoZero_EnterLockout(PRESSURE_AUTO_ZERO_FAULT_SAMPLE);
    }
    return;
  }
  g_invalid_count = 0U;

  switch (g_state) {
  case PRESSURE_AUTO_ZERO_WAIT_FACTORY_EMPTY:
    if (known_device != 0U) {
      PressureAutoZero_EnterLockout(PRESSURE_AUTO_ZERO_FAULT_BOOT_LOADED);
      break;
    }
    PressureAutoZero_ProcessEmpty(raw_value, 1U);
    break;

  case PRESSURE_AUTO_ZERO_WAIT_BOOT_EMPTY:
    if (known_device != 0U) {
      PressureAutoZero_EnterLockout(PRESSURE_AUTO_ZERO_FAULT_BOOT_LOADED);
      break;
    }
    PressureAutoZero_ProcessEmpty(raw_value, 0U);
    break;

  case PRESSURE_AUTO_ZERO_WAIT_DEVICE:
    if (known_device == 0U) {
      PressureAutoZero_ResetWindow();
      PressureAutoZero_ResetDevice();
      break;
    }
    if (device_code != g_last_device_code) {
      g_last_device_code = device_code;
      g_device_stable_count = 1U;
      PressureAutoZero_ResetWindow();
      break;
    }
    if (g_device_stable_count < PRESSURE_AUTO_ZERO_DEVICE_STABLE_FRAMES) {
      g_device_stable_count++;
    }
    if (g_device_stable_count >= PRESSURE_AUTO_ZERO_DEVICE_STABLE_FRAMES) {
      g_state = PRESSURE_AUTO_ZERO_CAPTURE_PRELOAD;
      PressureAutoZero_ResetWindow();
    }
    break;

  case PRESSURE_AUTO_ZERO_CAPTURE_PRELOAD:
    if ((known_device == 0U) || (device_code != g_last_device_code)) {
      g_state = PRESSURE_AUTO_ZERO_WAIT_DEVICE;
      PressureAutoZero_ResetWindow();
      PressureAutoZero_ResetDevice();
      break;
    }
    if (PressureAutoZero_AddStableSample(raw_value, &mean_raw) == 0U) {
      break;
    }
    preload_delta = (int64_t)mean_raw - (int64_t)g_boot_empty_raw;
    if (preload_delta < PRESSURE_AUTO_ZERO_PRELOAD_NEG_TOL_RAW) {
      PressureAutoZero_EnterLockout(PRESSURE_AUTO_ZERO_FAULT_PRELOAD_LOW);
    } else {
      preload_x10 = (preload_delta > 0x7FFFFFFFLL) ?
          0xFFFFFFFFUL :
          WeightCalibration_ApplyFromEmptyX10((int32_t)preload_delta);
      if (preload_x10 >= PRESSURE_AUTO_ZERO_PRELOAD_HARD_X10) {
        PressureAutoZero_EnterLockout(PRESSURE_AUTO_ZERO_FAULT_PRELOAD_HARD);
      } else if (preload_x10 > PRESSURE_AUTO_ZERO_PRELOAD_MAX_X10) {
        PressureAutoZero_EnterLockout(PRESSURE_AUTO_ZERO_FAULT_PRELOAD_HIGH);
      } else {
        g_session_zero_raw = mean_raw;
        g_state = PRESSURE_AUTO_ZERO_READY;
        g_fault = PRESSURE_AUTO_ZERO_FAULT_NONE;
        g_negative_count = 0U;
      }
    }
    break;

  case PRESSURE_AUTO_ZERO_READY:
    if ((known_device == 0U) || (device_code != g_last_device_code)) {
      g_state = PRESSURE_AUTO_ZERO_WAIT_EMPTY_RECOVERY;
      g_fault = PRESSURE_AUTO_ZERO_FAULT_DEVICE_CHANGED;
      g_negative_count = 0U;
      PressureAutoZero_ResetWindow();
      PressureAutoZero_ResetDevice();
      break;
    }
    pressure_delta = (int64_t)raw_value - (int64_t)g_session_zero_raw;
    if ((pressure_delta < 0LL) &&
        ((pressure_delta < -0x7FFFFFFFLL) ||
         (WeightCalibration_ApplyFromEmptyX10((int32_t)(-pressure_delta)) >
          PRESSURE_AUTO_ZERO_NEGATIVE_FAULT_X10))) {
      if (g_negative_count < PRESSURE_AUTO_ZERO_NEGATIVE_LIMIT) {
        g_negative_count++;
      }
      if (g_negative_count >= PRESSURE_AUTO_ZERO_NEGATIVE_LIMIT) {
        PressureAutoZero_EnterLockout(PRESSURE_AUTO_ZERO_FAULT_ZERO_LOST);
      }
    } else {
      g_negative_count = 0U;
    }
    break;

  case PRESSURE_AUTO_ZERO_WAIT_EMPTY_RECOVERY:
  case PRESSURE_AUTO_ZERO_LOCKOUT:
    if (known_device != 0U) {
      PressureAutoZero_ResetWindow();
      break;
    }
    if (g_factory_valid == 0U) {
      PressureAutoZero_ProcessEmpty(raw_value, 1U);
    } else {
      PressureAutoZero_ProcessEmpty(raw_value, 0U);
    }
    break;

  default:
    PressureAutoZero_EnterLockout(PRESSURE_AUTO_ZERO_FAULT_SAMPLE);
    break;
  }
}

uint8_t PressureAutoZero_IsReady(void)
{
  return (g_state == PRESSURE_AUTO_ZERO_READY) ? 1U : 0U;
}

int32_t PressureAutoZero_GetFactoryEmptyRaw(void)
{
  return g_factory_empty_raw;
}

int32_t PressureAutoZero_GetBootEmptyRaw(void)
{
  return g_boot_empty_raw;
}

int32_t PressureAutoZero_GetSessionZeroRaw(void)
{
  return g_session_zero_raw;
}

PressureAutoZeroState_t PressureAutoZero_GetState(void)
{
  return g_state;
}

PressureAutoZeroFault_t PressureAutoZero_GetFault(void)
{
  return g_fault;
}
