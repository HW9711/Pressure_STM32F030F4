#include "cs1237.h"

/* CS1237 写配置寄存器命令，来自手册 2.6.7.1 */
#define CS1237_CMD_WRITE_CONFIG 0x65U

/* 比例系数：用于把原始码值换算成最终工程量 */
static uint32_t g_cs1237_scale_factor = 1206U;
/* 去皮值：保存空载时的原始 ADC 读数 */
static int32_t g_cs1237_tare_raw = 0;
/* 当前芯片配置缓存，避免每次修改时都从外部重建 */
static uint8_t g_cs1237_config =
    (uint8_t)((CS1237_REFO_OFF_DEFAULT ? 0x40U : 0x00U) |
              ((uint8_t)CS1237_SPEED_DEFAULT << 4) |
              ((uint8_t)CS1237_GAIN_DEFAULT << 2) |
              (uint8_t)CS1237_CHANNEL_DEFAULT);

/* 根据端口号使能对应 GPIO 时钟，保证后续引脚初始化可用 */
static void CS1237_EnableGpioClock(GPIO_TypeDef *port)
{
    if (port == GPIOA) {
        __HAL_RCC_GPIOA_CLK_ENABLE();
    }
#ifdef GPIOB
    else if (port == GPIOB) {
        __HAL_RCC_GPIOB_CLK_ENABLE();
    }
#endif
#ifdef GPIOC
    else if (port == GPIOC) {
        __HAL_RCC_GPIOC_CLK_ENABLE();
    }
#endif
#ifdef GPIOF
    else if (port == GPIOF) {
        __HAL_RCC_GPIOF_CLK_ENABLE();
    }
#endif
}

/* 短延时：用于产生 CS1237 需要的最小脉宽 */
static void CS1237_DelayShort(void)
{
    volatile uint8_t i;

    for (i = 0U; i < 16U; i++) {
        __NOP();
    }
}

/* 拉低串行时钟脚 */
static void CS1237_SckLow(void)
{
    HAL_GPIO_WritePin(CS1237_SCK_GPIO_PORT, CS1237_SCK_GPIO_PIN, GPIO_PIN_RESET);
}

/* 拉高串行时钟脚 */
static void CS1237_SckHigh(void)
{
    HAL_GPIO_WritePin(CS1237_SCK_GPIO_PORT, CS1237_SCK_GPIO_PIN, GPIO_PIN_SET);
}

/* 读取数据引脚电平 */
static GPIO_PinState CS1237_ReadDataPin(void)
{
    return HAL_GPIO_ReadPin(CS1237_DOUT_GPIO_PORT, CS1237_DOUT_GPIO_PIN);
}

/* 将数据脚配置为输入模式，读取转换结果时使用 */
static void CS1237_SetDataPinInput(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    GPIO_InitStruct.Pin = CS1237_DOUT_GPIO_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(CS1237_DOUT_GPIO_PORT, &GPIO_InitStruct);
}

/* 将数据脚配置为推挽输出模式，写命令时使用 */
static void CS1237_SetDataPinOutput(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    GPIO_InitStruct.Pin = CS1237_DOUT_GPIO_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(CS1237_DOUT_GPIO_PORT, &GPIO_InitStruct);
}

/* 输出一个数据脚状态，用于发送命令和控制时序 */
static void CS1237_WriteDataPin(GPIO_PinState state)
{
    HAL_GPIO_WritePin(CS1237_DOUT_GPIO_PORT, CS1237_DOUT_GPIO_PIN, state);
}

/* 产生一个完整的时钟脉冲，上升沿和下降沿之间加入短延时 */
static void CS1237_PulseClock(void)
{
    CS1237_SckHigh();
    CS1237_DelayShort();
    CS1237_SckLow();
    CS1237_DelayShort();
}

/* 等待芯片进入可读状态，超时则返回失败 */
static uint8_t CS1237_WaitReady(uint32_t timeout_ms)
{
    uint32_t start_tick = HAL_GetTick();

    while (CS1237_ReadDataPin() == GPIO_PIN_SET) {
        if ((HAL_GetTick() - start_tick) > timeout_ms) {
            return 0U;
        }
    }

    return 1U;
}

/* 升序排序，用于中值滤波前对样本排序 */
static void CS1237_SortAsc(int32_t *buf, uint8_t len)
{
    uint8_t i;

    for (i = 1U; i < len; i++) {
        int32_t key = buf[i];
        int8_t j = (int8_t)i - 1;

        while ((j >= 0) && (buf[j] > key)) {
            buf[j + 1] = buf[j];
            j--;
        }

        buf[j + 1] = key;
    }
}

/* 读取一帧完整数据：24 位转换值，加上后续状态位与下一个配置位信息 */
static int32_t CS1237_ReadDataFrame(uint8_t *update1, uint8_t *update2)
{
    uint8_t i;
    uint32_t raw24 = 0U;

    if (update1 != NULL) {
        *update1 = 0U;
    }
    if (update2 != NULL) {
        *update2 = 0U;
    }

    if (!CS1237_WaitReady(CS1237_READY_TIMEOUT_MS)) {
        return 0;
    }

    /* 进入读取流程前，先把数据脚切回输入态 */
    CS1237_SetDataPinInput();
    CS1237_SckLow();

    /* 逐位移入 24 位原始数据，高位先出 */
    for (i = 0U; i < 24U; i++) {
        CS1237_SckHigh();
        CS1237_DelayShort();
        raw24 <<= 1U;
        if (CS1237_ReadDataPin() == GPIO_PIN_SET) {
            raw24 |= 0x01U;
        }
        CS1237_SckLow();
        CS1237_DelayShort();
    }

    /* 第 25 个脉冲采样状态位 1 */
    CS1237_SckHigh();
    CS1237_DelayShort();
    if ((update1 != NULL) && (CS1237_ReadDataPin() == GPIO_PIN_SET)) {
        *update1 = 1U;
    }
    CS1237_SckLow();
    CS1237_DelayShort();

    /* 第 26 个脉冲采样状态位 2 */
    CS1237_SckHigh();
    CS1237_DelayShort();
    if ((update2 != NULL) && (CS1237_ReadDataPin() == GPIO_PIN_SET)) {
        *update2 = 1U;
    }
    CS1237_SckLow();
    CS1237_DelayShort();

    /* 第 27 个脉冲用于把配置写入芯片的下一次转换周期 */
    CS1237_PulseClock();

    /* 24 位有符号数扩展到 32 位 */
    if ((raw24 & 0x800000U) != 0U) {
        raw24 |= 0xFF000000U;
    }

    return (int32_t)raw24;
}

/* 向 CS1237 写配置寄存器，完成当前配置的真正下发 */
static HAL_StatusTypeDef CS1237_WriteConfigRegister(uint8_t config)
{
    uint8_t i;

    /* 先读掉一帧旧数据，避免配置写入与数据读取互相干扰 */
    (void)CS1237_ReadDataFrame(NULL, NULL);

    /* 通过将数据脚切换为输出，开始发送写配置命令 */
    CS1237_SetDataPinOutput();
    CS1237_WriteDataPin(GPIO_PIN_SET);

    /* 前导时钟，进入命令发送阶段 */
    CS1237_PulseClock();
    CS1237_PulseClock();

    /* 发送写配置命令的 7 位序列 */
    for (i = 0U; i < 7U; i++) {
        GPIO_PinState bit_state =
            ((CS1237_CMD_WRITE_CONFIG << i) & 0x40U) ? GPIO_PIN_SET : GPIO_PIN_RESET;
        CS1237_WriteDataPin(bit_state);
        CS1237_PulseClock();
    }

    /* 补一个起始位，随后发送 8 位配置内容 */
    CS1237_WriteDataPin(GPIO_PIN_SET);
    CS1237_PulseClock();

    /* 将配置字按位送入芯片，最高位先发 */
    for (i = 0U; i < 8U; i++) {
        GPIO_PinState bit_state =
            ((config << i) & 0x80U) ? GPIO_PIN_SET : GPIO_PIN_RESET;
        CS1237_WriteDataPin(bit_state);
        CS1237_PulseClock();
    }

    /* 释放数据脚，恢复输入模式，准备后续读数 */
    CS1237_SetDataPinInput();
    CS1237_PulseClock();

    /* 缓存当前配置，供 SetGain / SetSpeed 复用 */
    g_cs1237_config = (uint8_t)(config & 0x7FU);

    return HAL_OK;
}

/* 初始化 CS1237 所依赖的 GPIO 外设和引脚方向 */
void CS1237_GPIO_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    /* 先打开数据脚和时钟脚所在端口的外设时钟 */
    CS1237_EnableGpioClock(CS1237_DOUT_GPIO_PORT);
    CS1237_EnableGpioClock(CS1237_SCK_GPIO_PORT);

    /* 时钟脚配置为推挽输出，直接驱动高低电平 */
    GPIO_InitStruct.Pin = CS1237_SCK_GPIO_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(CS1237_SCK_GPIO_PORT, &GPIO_InitStruct);

    /* 数据脚默认作为输入，并带上拉，避免悬空 */
    CS1237_SetDataPinInput();
    CS1237_SckLow();
}

/* 驱动初始化：完成 GPIO 初始化并写入默认配置 */
HAL_StatusTypeDef CS1237_Init(void)
{
    CS1237_GPIO_Init();
    HAL_Delay(5U);

    return CS1237_WriteConfigRegister(g_cs1237_config);
}

/* 一次性修改全部配置项，并同步更新到芯片 */
HAL_StatusTypeDef CS1237_SetConfig(CS1237_Gain_t gain,
                                   CS1237_Speed_t speed,
                                   CS1237_Channel_t channel,
                                   uint8_t refo_off)
{
    uint8_t config;

    /* 按芯片寄存器位定义重新拼装配置字 */
    config = (uint8_t)(((refo_off != 0U) ? 0x40U : 0x00U) |
                       (((uint8_t)speed & 0x03U) << 4) |
                       (((uint8_t)gain & 0x03U) << 2) |
                       ((uint8_t)channel & 0x03U));

    return CS1237_WriteConfigRegister(config);
}

/* 只修改增益，保持速度、通道和参考源设置不变 */
HAL_StatusTypeDef CS1237_SetGain(CS1237_Gain_t gain)
{
    return CS1237_SetConfig(gain,
                            (CS1237_Speed_t)((g_cs1237_config >> 4) & 0x03U),
                            (CS1237_Channel_t)(g_cs1237_config & 0x03U),
                            (uint8_t)((g_cs1237_config >> 6) & 0x01U));
}

/* 只修改输出速率，其他配置沿用当前缓存值 */
HAL_StatusTypeDef CS1237_SetSpeed(CS1237_Speed_t speed)
{
    return CS1237_SetConfig((CS1237_Gain_t)((g_cs1237_config >> 2) & 0x03U),
                            speed,
                            (CS1237_Channel_t)(g_cs1237_config & 0x03U),
                            (uint8_t)((g_cs1237_config >> 6) & 0x01U));
}

/* 查询当前数据输出脚是否拉低，拉低表示数据已准备好 */
uint8_t CS1237_IsReady(void)
{
    return (CS1237_ReadDataPin() == GPIO_PIN_RESET) ? 1U : 0U;
}

/* 读取一帧原始有符号值，不做额外换算 */
int32_t CS1237_ReadRawSigned(void)
{
    return CS1237_ReadDataFrame(NULL, NULL);
}

/* 连续读取若干次原始值后排序，并返回中值，用于抑制尖峰噪声 */
int32_t CS1237_ReadMedian(uint8_t samples)
{
    int32_t buf[CS1237_MEDIAN_LEN];
    uint8_t i;
    uint8_t median_idx;

    /* 允许外部传 0，表示默认使用预设中值长度 */
    if (samples == 0U) {
        samples = CS1237_MEDIAN_LEN;
    }
    /* 防止超过缓冲区长度 */
    if (samples > CS1237_MEDIAN_LEN) {
        samples = CS1237_MEDIAN_LEN;
    }

    /* 逐个采样，形成滤波样本 */
    for (i = 0U; i < samples; i++) {
        buf[i] = CS1237_ReadRawSigned();
    }

    /* 排序后取中间值，得到比单次采样更稳定的结果 */
    CS1237_SortAsc(buf, samples);
    median_idx = samples / 2U;

    return buf[median_idx];
}

/* 设置换算比例系数，0 值无效，不覆盖当前配置 */
void CS1237_SetScaleFactor(uint32_t factor)
{
    if (factor == 0U) {
        return;
    }

    g_cs1237_scale_factor = factor;
}

/* 读取当前换算比例系数 */
uint32_t CS1237_GetScaleFactor(void)
{
    return g_cs1237_scale_factor;
}

/* 读取若干次中值并保存为去皮值 */
void CS1237_GetTare(uint8_t samples)
{
    g_cs1237_tare_raw = CS1237_ReadMedian(samples);
}

/* 获取当前保存的去皮原始值 */
int32_t CS1237_GetTareRaw(void)
{
    return g_cs1237_tare_raw;
}

/* 读取当前测量值，直接按原始码值和比例系数换算 */
uint32_t CS1237_GetMeasurementX10(void)
{
    int32_t raw_value = CS1237_ReadRawSigned();
    int64_t scaled;

    /* 原始码值为负时按 0 处理，避免返回无意义数值 */
    if (raw_value <= 0) {
        return 0U;
    }

    /* 采用整数运算完成缩放，减少浮点依赖 */
    scaled = (int64_t)raw_value * (int64_t)g_cs1237_scale_factor;
    scaled = (scaled + 500000LL) / 1000000LL;

    if (scaled < 0) {
        return 0U;
    }

    return (uint32_t)scaled;
}

uint32_t CS1237_GetMeasurement(void)
{
    uint32_t value_x10 = CS1237_GetMeasurementX10();

    return (value_x10 + 5U) / 10U;
}
