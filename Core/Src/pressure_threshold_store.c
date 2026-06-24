#include "pressure_threshold_store.h"

/* 阈值记录，按 32bit 对齐，便于按字写入 Flash */
typedef struct {
  uint32_t magic;        /* 魔术字，标识该页已写入有效阈值结构 */
  uint32_t threshold_g;  /* 阈值，单位 g */
  uint32_t check;        /* 完整性校验值 */
} PressureThresholdFlash_t;

static uint16_t g_pressure_threshold_runtime_g = PRESSURE_THRESHOLD_DEFAULT_G;

uint8_t PressureThreshold_IsValid(uint16_t threshold_g)
{
  return ((threshold_g > 0U) && (threshold_g <= PRESSURE_THRESHOLD_MAX_G)) ? 1U : 0U;
}

uint16_t PressureThreshold_GetRuntime(void)
{
  return g_pressure_threshold_runtime_g;
}

void PressureThreshold_SetRuntime(uint16_t threshold_g)
{
  if (PressureThreshold_IsValid(threshold_g) != 0U) {
    g_pressure_threshold_runtime_g = threshold_g;
  }
}

uint8_t PressureThreshold_SetRuntimeAndSave(uint16_t threshold_g)
{
  if (PressureThreshold_IsValid(threshold_g) == 0U) {
    return 0U;
  }
  if (PressureThreshold_Save(threshold_g) == 0U) {
    return 0U;
  }
  g_pressure_threshold_runtime_g = threshold_g;
  return 1U;
}

/* 生成简单校验值，防止上电读到半写入或脏数据 */
#if (PRESSURE_THRESHOLD_FLASH_STORE_ENABLE != 0U)
static uint32_t PressureThreshold_MakeCheck(uint32_t threshold_g)
{
  return (PRESSURE_THRESHOLD_FLASH_MAGIC ^ threshold_g ^ 0xA5A55A5AUL);
}

/* 将阈值写入内部 Flash（使用末页，擦除后重写） */
#endif
uint8_t PressureThreshold_Save(uint16_t threshold_g)
{
#if (PRESSURE_THRESHOLD_FLASH_STORE_ENABLE != 0U)
  FLASH_EraseInitTypeDef erase_init;         /* Flash 擦除参数结构体 */
  uint32_t page_error = 0U;                  /* 擦除失败页号返回值 */
  PressureThresholdFlash_t store_data;       /* 待写入的阈值结构 */
  HAL_StatusTypeDef hal_ret;                 /* HAL 接口返回值 */

  /* 运行时参数保护，避免写入明显异常阈值 */
  if (PressureThreshold_IsValid(threshold_g) == 0U) {
    return 0U;
  }

  /* 组织要写入的数据 */
  store_data.magic = PRESSURE_THRESHOLD_FLASH_MAGIC;
  store_data.threshold_g = (uint32_t)threshold_g;
  store_data.check = PressureThreshold_MakeCheck(store_data.threshold_g);

  /* 解锁 Flash，准备擦写 */
  HAL_FLASH_Unlock();

  /* 目标页仅保存这一组阈值数据 */
  erase_init.TypeErase = FLASH_TYPEERASE_PAGES;
  erase_init.PageAddress = PRESSURE_THRESHOLD_FLASH_ADDR;
  erase_init.NbPages = 1U;

  /* 先擦除目标页 */
  hal_ret = HAL_FLASHEx_Erase(&erase_init, &page_error);
  if (hal_ret != HAL_OK) {
    HAL_FLASH_Lock();
    return 0U;
  }

  /* 依次按字编程写入 3 个 32bit 数据 */
  hal_ret = HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD,
                              PRESSURE_THRESHOLD_FLASH_ADDR,
                              store_data.magic);
  if (hal_ret != HAL_OK) {
    HAL_FLASH_Lock();
    return 0U;
  }

  hal_ret = HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD,
                              PRESSURE_THRESHOLD_FLASH_ADDR + 4U,
                              store_data.threshold_g);
  if (hal_ret != HAL_OK) {
    HAL_FLASH_Lock();
    return 0U;
  }

  hal_ret = HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD,
                              PRESSURE_THRESHOLD_FLASH_ADDR + 8U,
                              store_data.check);
  if (hal_ret != HAL_OK) {
    HAL_FLASH_Lock();
    return 0U;
  }

  /* 上锁 Flash，结束写入流程 */
  HAL_FLASH_Lock();
  return 1U;
#else
  (void)threshold_g;
  return 0U;
#endif
}

/* 从内部 Flash 读取阈值，校验通过才认为有效 */
uint8_t PressureThreshold_Load(uint16_t *threshold_g)
{
#if (PRESSURE_THRESHOLD_FLASH_STORE_ENABLE != 0U)
  const PressureThresholdFlash_t *store_data =
      (const PressureThresholdFlash_t *)PRESSURE_THRESHOLD_FLASH_ADDR;

  /* 输出指针保护 */
  if (threshold_g == NULL) {
    return 0U;
  }

  /* 魔术字不匹配，说明未初始化或页数据无效 */
  if (store_data->magic != PRESSURE_THRESHOLD_FLASH_MAGIC) {
    return 0U;
  }

  /* 校验不通过，判定数据损坏 */
  if (store_data->check != PressureThreshold_MakeCheck(store_data->threshold_g)) {
    return 0U;
  }

  /* 范围检查，避免异常值影响业务 */
  if ((store_data->threshold_g == 0U) || (store_data->threshold_g > PRESSURE_THRESHOLD_MAX_G)) {
    return 0U;
  }

  /* 读出有效阈值 */
  *threshold_g = (uint16_t)store_data->threshold_g;
  return 1U;
#else
  (void)threshold_g;
  return 0U;
#endif
}
