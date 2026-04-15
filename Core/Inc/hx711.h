#ifndef __HX711_H__
#define __HX711_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"

/* HX711 DOUT 引脚：保持当前配置为 PA7 */
#define HX711_DOUT_GPIO_PORT GPIOA
#define HX711_DOUT_GPIO_PIN  GPIO_PIN_7

/* HX711 SCK 引脚：保持当前配置为 PA5 */
#define HX711_SCK_GPIO_PORT  GPIOA
#define HX711_SCK_GPIO_PIN   GPIO_PIN_5

/* HX711 增益脉冲选择：1=A128，2=B32，3=A64 */
typedef enum {
	HX711_GAIN_PULSE_A128 = 1,
	HX711_GAIN_PULSE_B32  = 2,
	HX711_GAIN_PULSE_A64  = 3
} HX711_GainPulse_t;

/*
 * HX711 默认增益宏：后续如需切换增益，只改这里
 * 可选：HX711_GAIN_PULSE_A128 / HX711_GAIN_PULSE_B32 / HX711_GAIN_PULSE_A64
 */
#define HX711_GAIN_DEFAULT HX711_GAIN_PULSE_B32

/* 按参考工程风格：初始化 HX711 GPIO */
void HX711_GPIO_Init(void);

/* 按参考工程风格：读取一次 HX711 原始值（24bit + 第25脉冲） */
uint32_t Read_HX711(void);

/* 读取一次带符号原始值（24bit 二补码扩展） */
int32_t HX711_ReadRawSigned(void);

/* 按参考工程风格：中值滤波读取（返回原始码） */
uint32_t HX711_ReadMedian(uint8_t samples);

/* 设置修正系数（对应参考工程 hx711_xishu） */
void HX711_SetScaleFactor(uint32_t factor);

/* 设置下一次采样的增益脉冲模式 */
void HX711_SetGainPulse(HX711_GainPulse_t gain_pulse);

/* 获取当前增益脉冲模式 */
HX711_GainPulse_t HX711_GetGainPulse(void);

/* 获取当前修正系数 */
uint32_t HX711_GetScaleFactor(void);

/* 执行去皮（内部自动做中值滤波） */
void HX711_GetTare(uint8_t samples);

/* 获取当前皮重（内部缩放值） */
uint32_t HX711_GetTareValue(void);

/* 获取当前皮重（原始码值） */
int32_t HX711_GetTareRaw(void);

/* 按参考工程公式计算重量，单位 g */
uint32_t HX711_GetWeight(void);

#ifdef __cplusplus
}
#endif

#endif /* __HX711_H__ */
