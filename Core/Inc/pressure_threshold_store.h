#ifndef __PRESSURE_THRESHOLD_STORE_H__
#define __PRESSURE_THRESHOLD_STORE_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"

/* 压力阈值默认值，单位 g */
#define PRESSURE_THRESHOLD_DEFAULT_G                 400U
/* 压力阈值有效上限，避免读到异常值后误动作，单位 g */
#define PRESSURE_THRESHOLD_MAX_G                     50000U
/* 压力阈值写入 Flash 功能总开关：0=关闭，1=开启 */
#define PRESSURE_THRESHOLD_FLASH_STORE_ENABLE        1U
/* 启动时是否强制写入宏定义阈值到 Flash：0=否，1=是（调试/首次配置可打开） */
#define PRESSURE_THRESHOLD_FORCE_WRITE_ON_BOOT       0U
/* 启动时强制写入的阈值，单位 g */
#define PRESSURE_THRESHOLD_FORCE_VALUE_G             399U
/* 阈值存储页起始地址：STM32F030F4 16KB Flash 最后一页(1KB) */
#define PRESSURE_THRESHOLD_FLASH_ADDR                0x08003C00U
/* 阈值存储魔术字，用于判断数据是否初始化 */
#define PRESSURE_THRESHOLD_FLASH_MAGIC               0x54485245UL

/* 从内部 Flash 读取阈值，读取失败返回 0 */
uint8_t PressureThreshold_Load(uint16_t *threshold_g);

/* 将阈值写入内部 Flash，写入失败返回 0 */
uint8_t PressureThreshold_Save(uint16_t threshold_g);
uint8_t PressureThreshold_IsValid(uint16_t threshold_g);
uint16_t PressureThreshold_GetRuntime(void);
void PressureThreshold_SetRuntime(uint16_t threshold_g);
uint8_t PressureThreshold_SetRuntimeAndSave(uint16_t threshold_g);

#ifdef __cplusplus
}
#endif

#endif /* __PRESSURE_THRESHOLD_STORE_H__ */
