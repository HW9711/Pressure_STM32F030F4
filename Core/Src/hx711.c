#include "hx711.h"

/* 中值滤波长度：沿用参考工程默认 5 点 */
#define HX711_MEDIAN_LEN 5U

/* 修正系数默认值（按 26.8g 实测点重新标定） */
static uint32_t g_hx711_scale_factor = 1206U;

/* 参考工程中的皮重变量（已做 0.01 缩放） */
static uint32_t g_hx711_tare = 0U;

/* 原始码值皮重（用于诊断接线与传感器响应） */
static int32_t g_hx711_tare_raw = 0;

/* 当前增益脉冲，默认使用统一宏配置 */
static HX711_GainPulse_t g_hx711_gain_pulse = HX711_GAIN_DEFAULT;

/* 按端口开启 GPIO 时钟，便于后续换端口 */
static void HX711_EnableGpioClock(GPIO_TypeDef *port)
{
    /* 如果端口是 GPIOA，则开启 GPIOA 时钟 */
    if (port == GPIOA) {
        __HAL_RCC_GPIOA_CLK_ENABLE();
    }
#ifdef GPIOB
    /* 如果端口是 GPIOB，则开启 GPIOB 时钟 */
    else if (port == GPIOB) {
        __HAL_RCC_GPIOB_CLK_ENABLE();
    }
#endif
#ifdef GPIOC
    /* 如果端口是 GPIOC，则开启 GPIOC 时钟 */
    else if (port == GPIOC) {
        __HAL_RCC_GPIOC_CLK_ENABLE();
    }
#endif
#ifdef GPIOF
    /* 如果端口是 GPIOF，则开启 GPIOF 时钟 */
    else if (port == GPIOF) {
        __HAL_RCC_GPIOF_CLK_ENABLE();
    }
#endif
}

/* 极短延时，用于 SCK 翻转间隔 */
static void HX711_DelayShort(void)
{
    /* 定义循环变量 */
    volatile uint8_t i;

    /* 空操作形成短延时，避免引脚切换过快 */
    for (i = 0U; i < 10U; i++) {
        __NOP();
    }
}

/* 把 SCK 拉低 */
static void HX711_SckLow(void)
{
    /* SCK 输出低电平 */
    HAL_GPIO_WritePin(HX711_SCK_GPIO_PORT, HX711_SCK_GPIO_PIN, GPIO_PIN_RESET);
}

/* 把 SCK 拉高 */
static void HX711_SckHigh(void)
{
    /* SCK 输出高电平 */
    HAL_GPIO_WritePin(HX711_SCK_GPIO_PORT, HX711_SCK_GPIO_PIN, GPIO_PIN_SET);
}

/* 读取 DOUT 电平 */
static GPIO_PinState HX711_ReadDataPin(void)
{
    /* 读取 DOUT 当前电平并返回 */
    return HAL_GPIO_ReadPin(HX711_DOUT_GPIO_PORT, HX711_DOUT_GPIO_PIN);
}

/* 原地升序排序（插入排序） */
static void HX711_SortAsc(uint32_t *buf, uint8_t len)
{
    /* 外层循环变量 */
    uint8_t i;

    /* 从第二个元素开始插入 */
    for (i = 1U; i < len; i++) {
        /* 保存待插入值 */
        uint32_t key = buf[i];

        /* 内层索引需要可减到 -1 */
        int8_t j = (int8_t)i - 1;

        /* 找到插入位置并搬移更大元素 */
        while ((j >= 0) && (buf[j] > key)) {
            buf[j + 1] = buf[j];
            j--;
        }

        /* 把待插入值放到最终位置 */
        buf[j + 1] = key;
    }
}

void HX711_GPIO_Init(void)
{
    /* 定义 GPIO 初始化结构体 */
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    /* 使能 DOUT/SCK 对应端口时钟 */
    HX711_EnableGpioClock(HX711_DOUT_GPIO_PORT);
    HX711_EnableGpioClock(HX711_SCK_GPIO_PORT);

    /* 配置 SCK 为推挽输出 */
    GPIO_InitStruct.Pin = HX711_SCK_GPIO_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(HX711_SCK_GPIO_PORT, &GPIO_InitStruct);

    /* 配置 DOUT 为输入上拉（等效参考工程输入模式） */
    GPIO_InitStruct.Pin = HX711_DOUT_GPIO_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(HX711_DOUT_GPIO_PORT, &GPIO_InitStruct);

    /* 空闲时把 SCK 拉低，避免进入掉电 */
    HX711_SckLow();
}

uint32_t Read_HX711(void)
{
    /* 位循环变量 */
    uint8_t i;

    /* 额外脉冲计数变量 */
    uint8_t pulse_count;

    /* 保存最终 24bit 读数 */
    uint32_t value = 0U;

    /* 先确保 SCK 处于低电平 */
    HX711_SckLow();

    /* 记录等待起始时刻，用于超时保护 */
    uint32_t start_tick = HAL_GetTick();

    /* 等待 DOUT 拉低，表示本次转换数据就绪 */
    while (HX711_ReadDataPin() == GPIO_PIN_SET) {
        /* 超过 200ms 仍未就绪则返回 0，避免主循环卡死 */
        if ((HAL_GetTick() - start_tick) > 200U) {
            return 0U;
        }
    }

    /* 逐位读取 24bit 数据（MSB first） */
    for (i = 0U; i < 24U; i++) {
        /* 上升沿触发下一位输出 */
        HX711_SckHigh();

        /* 插入极短延时，增加时序裕量 */
        HX711_DelayShort();

        /* 左移给新位腾位置 */
        value <<= 1U;

        /* 读取 DOUT 位值并并入最低位 */
        if (HX711_ReadDataPin() == GPIO_PIN_SET) {
            value |= 0x01U;
        }

        /* 拉低 SCK 完成一个时钟周期 */
        HX711_SckLow();

        /* 再加短延时，避免脉冲太窄 */
        HX711_DelayShort();
    }

    /* 输出 1/2/3 个附加脉冲，选择下一次通道/增益 */
    pulse_count = (uint8_t)g_hx711_gain_pulse;
    for (i = 0U; i < pulse_count; i++) {
        HX711_SckHigh();
        HX711_DelayShort();
        HX711_SckLow();
        HX711_DelayShort();
    }

    /* 沿用参考工程处理：对 24bit 数据做异或偏置 */
    value ^= 0x800000U;

    /* 返回处理后的原始值 */
    return value;
}

int32_t HX711_ReadRawSigned(void)
{
    /* 保存按参考工程读取的原始值（异或偏置后） */
    uint32_t val = Read_HX711();

    /* 先还原回标准 24bit 二补码 */
    uint32_t raw24 = val ^ 0x800000U;

    /* 对 24bit 符号位做扩展到 32bit */
    if ((raw24 & 0x800000U) != 0U) {
        raw24 |= 0xFF000000U;
    }

    /* 返回有符号原始码值 */
    return (int32_t)raw24;
}

uint32_t HX711_ReadMedian(uint8_t samples)
{
    /* 定义采样缓存 */
    uint32_t buf[HX711_MEDIAN_LEN];

    /* 循环变量 */
    uint8_t i;

    /* 中值索引 */
    uint8_t median_idx;

    /* 防止传入 0 */
    if (samples == 0U) {
        samples = HX711_MEDIAN_LEN;
    }

    /* 为防越界，最大不超过本地缓存长度 */
    if (samples > HX711_MEDIAN_LEN) {
        samples = HX711_MEDIAN_LEN;
    }

    /* 连续读取 N 次原始值 */
    for (i = 0U; i < samples; i++) {
        buf[i] = Read_HX711();
    }

    /* 对采样值做升序排序 */
    HX711_SortAsc(buf, samples);

    /* 计算中值下标 */
    median_idx = samples / 2U;

    /* 返回中值结果 */
    return buf[median_idx];
}

void HX711_SetScaleFactor(uint32_t factor)
{
    /* 系数为 0 没有意义，直接忽略 */
    if (factor == 0U) {
        return;
    }

    /* 更新修正系数 */
    g_hx711_scale_factor = factor;
}

void HX711_SetGainPulse(HX711_GainPulse_t gain_pulse)
{
    /* 仅允许 1/2/3 三种合法脉冲模式 */
    if ((gain_pulse != HX711_GAIN_PULSE_A128) &&
        (gain_pulse != HX711_GAIN_PULSE_B32) &&
        (gain_pulse != HX711_GAIN_PULSE_A64)) {
        return;
    }

    /* 更新增益配置 */
    g_hx711_gain_pulse = gain_pulse;
}

HX711_GainPulse_t HX711_GetGainPulse(void)
{
    /* 返回当前增益模式 */
    return g_hx711_gain_pulse;
}

uint32_t HX711_GetScaleFactor(void)
{
    /* 返回当前修正系数 */
    return g_hx711_scale_factor;
}

void HX711_GetTare(uint8_t samples)
{
    /* 保存滤波后的原始值 */
    uint32_t raw;

    /* 做中值滤波读取，减少零点抖动 */
    raw = HX711_ReadMedian(samples);

    /* 沿用参考工程缩放方式：raw * 0.01 */
    g_hx711_tare = (uint32_t)((float)raw * 0.01f);

    /* 保存原始皮重码值，便于后续输出净差值 */
    g_hx711_tare_raw = HX711_ReadRawSigned();
}

uint32_t HX711_GetTareValue(void)
{
    /* 返回当前皮重 */
    return g_hx711_tare;
}

int32_t HX711_GetTareRaw(void)
{
    /* 返回原始皮重码值 */
    return g_hx711_tare_raw;
}

uint32_t HX711_GetWeight(void)
{
    /* 第一次采样值（缩放后） */
    uint32_t get;

    /* 第二次采样原始值 */
    uint32_t a;

    /* 净重中间值 */
    uint32_t aa;

    /* 先采样一次并按参考工程做 0.01 缩放 */
    get = (uint32_t)((float)Read_HX711() * 0.01f);

    /* 只有超过皮重才计算重量 */
    if (get > g_hx711_tare) {
        /* 再采样一次以降低瞬时误差 */
        a = Read_HX711();

        /* 计算去皮后的净值 */
        aa = (uint32_t)((float)a * 0.01f) - g_hx711_tare;

        /* 按参考工程公式计算重量（单位 g） */
        return (uint32_t)((float)aa * 0.00001f * (float)g_hx711_scale_factor);
    }

    /* 小于皮重时视为 0g */
    return 0U;
}
