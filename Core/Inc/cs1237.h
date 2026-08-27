#ifndef __CS1237_H__
#define __CS1237_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"

/* CS1237 数据输出脚与时钟脚的硬件连接定义 */
#define CS1237_DOUT_GPIO_PORT GPIOA
#define CS1237_DOUT_GPIO_PIN  GPIO_PIN_7

#define CS1237_SCK_GPIO_PORT  GPIOA
#define CS1237_SCK_GPIO_PIN   GPIO_PIN_5

/* 读数等待超时时间和中值滤波样本数 */
#define CS1237_READY_TIMEOUT_MS 400U
#define CS1237_MEDIAN_LEN       5U

/* CS1237 增益档位定义，对应芯片配置寄存器中的增益位 */
typedef enum {
    CS1237_GAIN_1   = 0U,
    CS1237_GAIN_2   = 1U,
    CS1237_GAIN_64  = 2U,
    CS1237_GAIN_128 = 3U
} CS1237_Gain_t;

/* CS1237 输出速率定义，对应芯片配置寄存器中的速率位 */
typedef enum {
    CS1237_SPEED_10HZ   = 0U,
    CS1237_SPEED_40HZ   = 1U,
    CS1237_SPEED_640HZ  = 2U,
    CS1237_SPEED_1280HZ = 3U
} CS1237_Speed_t;

/* CS1237 通道选择定义 */
typedef enum {
    CS1237_CHANNEL_A     = 0U,
    CS1237_CHANNEL_RSVD1 = 1U,
    CS1237_CHANNEL_TEMP  = 2U,
    CS1237_CHANNEL_SHORT = 3U
} CS1237_Channel_t;

/* 默认上电配置，便于初始化时直接写入芯片 */
#define CS1237_GAIN_DEFAULT    CS1237_GAIN_128
#define CS1237_SPEED_DEFAULT   CS1237_SPEED_10HZ
#define CS1237_CHANNEL_DEFAULT CS1237_CHANNEL_A
#define CS1237_REFO_OFF_DEFAULT 0U

/* GPIO 初始化与驱动初始化 */
void CS1237_GPIO_Init(void);
HAL_StatusTypeDef CS1237_Init(void);

/* 整体配置接口，允许一次设置增益、速率、通道和参考源关闭位 */
HAL_StatusTypeDef CS1237_SetConfig(CS1237_Gain_t gain,
                                   CS1237_Speed_t speed,
                                   CS1237_Channel_t channel,
                                   uint8_t refo_off);

/* 仅修改增益或速率，其余配置保持不变 */
HAL_StatusTypeDef CS1237_SetGain(CS1237_Gain_t gain);
HAL_StatusTypeDef CS1237_SetSpeed(CS1237_Speed_t speed);

/* 设备就绪查询、原始值读取和中值滤波读取 */
uint8_t CS1237_IsReady(void);
int32_t CS1237_ReadRawSigned(void);
int32_t CS1237_ReadMedian(uint8_t samples);
uint8_t CS1237_ReadMedianChecked(uint8_t samples, int32_t *raw_value);

/* 比例系数相关接口，通常用于把原始值换算成工程量 */
void CS1237_SetScaleFactor(uint32_t factor);
uint32_t CS1237_GetScaleFactor(void);

/* 去皮相关接口：采样并保存空载原始值 */
void CS1237_GetTare(uint8_t samples);
int32_t CS1237_GetTareRaw(void);

/* 返回去皮后的测量结果，已经按比例系数换算 */
uint32_t CS1237_GetMeasurementX10(void);
uint32_t CS1237_GetMeasurement(void);

#ifdef __cplusplus
}
#endif

#endif /* __CS1237_H__ */
