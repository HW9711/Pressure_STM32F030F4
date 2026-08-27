#include "pressure_zero_store.h"

typedef struct {
  uint32_t magic;
  uint32_t version;
  int32_t factory_empty_raw;
  uint32_t check;
} PressureZeroFlash_t;

static uint32_t PressureZeroStore_MakeCheck(int32_t factory_empty_raw)
{
  return PRESSURE_ZERO_FLASH_MAGIC ^
         PRESSURE_ZERO_FLASH_VERSION ^
         (uint32_t)factory_empty_raw ^
         0xA55A3CC3UL;
}

uint8_t PressureZeroStore_Load(int32_t *factory_empty_raw)
{
  const PressureZeroFlash_t *store_data =
      (const PressureZeroFlash_t *)PRESSURE_ZERO_FLASH_ADDR;

  if (factory_empty_raw == NULL) {
    return 0U;
  }
  if ((store_data->magic != PRESSURE_ZERO_FLASH_MAGIC) ||
      (store_data->version != PRESSURE_ZERO_FLASH_VERSION)) {
    return 0U;
  }
  if (store_data->check != PressureZeroStore_MakeCheck(store_data->factory_empty_raw)) {
    return 0U;
  }

  *factory_empty_raw = store_data->factory_empty_raw;
  return 1U;
}

uint8_t PressureZeroStore_Save(int32_t factory_empty_raw)
{
  FLASH_EraseInitTypeDef erase_init;
  PressureZeroFlash_t store_data;
  const uint32_t *words = (const uint32_t *)&store_data;
  uint32_t page_error = 0U;
  uint32_t i;
  int32_t verify_raw;

  store_data.magic = PRESSURE_ZERO_FLASH_MAGIC;
  store_data.version = PRESSURE_ZERO_FLASH_VERSION;
  store_data.factory_empty_raw = factory_empty_raw;
  store_data.check = PressureZeroStore_MakeCheck(factory_empty_raw);

  HAL_FLASH_Unlock();
  erase_init.TypeErase = FLASH_TYPEERASE_PAGES;
  erase_init.PageAddress = PRESSURE_ZERO_FLASH_ADDR;
  erase_init.NbPages = 1U;

  if (HAL_FLASHEx_Erase(&erase_init, &page_error) != HAL_OK) {
    HAL_FLASH_Lock();
    return 0U;
  }

  for (i = 0U; i < (sizeof(store_data) / sizeof(uint32_t)); i++) {
    if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD,
                          PRESSURE_ZERO_FLASH_ADDR + (i * 4U),
                          words[i]) != HAL_OK) {
      HAL_FLASH_Lock();
      return 0U;
    }
  }

  HAL_FLASH_Lock();
  if ((PressureZeroStore_Load(&verify_raw) == 0U) || (verify_raw != factory_empty_raw)) {
    return 0U;
  }
  return 1U;
}
