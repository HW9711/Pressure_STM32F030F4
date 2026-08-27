#ifndef __PRESSURE_ZERO_STORE_H__
#define __PRESSURE_ZERO_STORE_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"

/* Former runtime-calibration page, now dedicated to the factory empty value. */
#define PRESSURE_ZERO_FLASH_ADDR       0x08003800U
#define PRESSURE_ZERO_FLASH_MAGIC      0x5A45524FUL
#define PRESSURE_ZERO_FLASH_VERSION    1U

uint8_t PressureZeroStore_Load(int32_t *factory_empty_raw);
uint8_t PressureZeroStore_Save(int32_t factory_empty_raw);

#ifdef __cplusplus
}
#endif

#endif /* __PRESSURE_ZERO_STORE_H__ */
