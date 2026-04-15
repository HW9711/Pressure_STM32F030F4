/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "usart.h"
#include "gpio.h"
#include "cs1237.h"
#include <stdio.h>
#include <math.h>  /* 添加数学库支持fabsf函数 */


/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* CS1237 原始重量换算基础系数 */
#define CS1237_SCALE_BASE            14751U
#define CS1237_SCALE_FACTOR          CS1237_SCALE_BASE
#define CS1237_SEGMENT_CAL_ENABLE    1U
#define CS1237_CAL_POINT_COUNT       6U
#define CS1237_CAL0_REAL_X10         0U
#define CS1237_CAL0_RAW              (-5446)
#define CS1237_CAL1_REAL_X10         218U
#define CS1237_CAL1_RAW              13082
#define CS1237_CAL2_REAL_X10         1218U
#define CS1237_CAL2_RAW              95588
#define CS1237_CAL3_REAL_X10         2218U
#define CS1237_CAL3_RAW              178174
#define CS1237_CAL4_REAL_X10         5218U
#define CS1237_CAL4_RAW              419128
#define CS1237_CAL5_REAL_X10         10218U
#define CS1237_CAL5_RAW              817285

/* 5点分段线性标定开关：0=关闭，1=开启 */

/* 标定点0：仅托盘，真实重量 / 当前显示重量，单位 0.1g */

/* 标定点1：托盘 + 100g砝码 */

/* 标定点2：托盘 + 200g砝码 */

/* 标定点3：托盘 + 500g砝码 */

/* 标定点4：托盘 + 1000g砝码 */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */
#define ADC_FILTER_WINDOW 10              /* 滤波窗口大小 */
#define ADC_FILTER_TRIM 1                  /* 排序后去掉最小/最大值 */
#define ADC_FILTER_ALPHA_FAST 0.25f        /* 快速跟踪系数 */
#define ADC_FILTER_ALPHA_SLOW 0.25f        /* 平稳跟踪系数 */
#define ADC_FILTER_JUMP_THRESHOLD 3.0f     /* 判断是否发生明显变化 */
#define WINDOW_SIZE ADC_FILTER_WINDOW      /* 兼容旧宏名 */
#define PRESSURE_ADC_INVERT 0
#define CALIBRATION_POINTS 5              /* 标定点数量 */
#define WEIGHT_CHANGE_THRESHOLD 10.0f     /* 重量变化阈值（克） */
#define STABLE_SAMPLES_REQUIRED 3         /* 稳定样本数要求 */

/* ADC鲁棒滤波状态 */
static uint16_t adc_buffer[ADC_FILTER_WINDOW] = {0};
static uint8_t  adc_index = 0;
static uint8_t  adc_filled = 0;
static float    adc_ema = 0.0f;
static uint8_t  adc_ema_initialized = 0;

/* 标定参数 */
typedef struct {
    uint16_t adc_value;   /* ADC读数 */
    float weight_g;       /* 对应重量（克） */
} CalibrationPoint_t;

/* 默认标定参数（需要用户根据实际测量更新） */
static CalibrationPoint_t calibration_points[CALIBRATION_POINTS] = {
    {885, 0.0f},     /* 零点：0g */
    {890, 20.0f},
    {893, 50.0f},    /* 20g 标定点（示例值，需要实际测量） */
    {897, 100.0f}, 
    {904, 200.0f}     /* 100g 标定点（示例值，需要实际测量） */
};

/* 标定结果 */
static float calibration_slope = 0.0f;    /* 斜率：g/ADC */
static float calibration_offset = 0.0f;   /* 偏移：g */
static uint8_t calibration_valid = 0;     /* 标定是否有效 */

/* 增量检测算法状态 */
typedef struct {
    float current_weight;      /* 当前稳定重量 */
    float last_stable_weight;  /* 上一次稳定重量 */
    float weight_buffer[STABLE_SAMPLES_REQUIRED]; /* 重量缓冲区 */
    uint8_t buffer_index;      /* 缓冲区索引 */
    uint8_t stable_count;      /* 稳定计数 */
    uint8_t weight_stable;     /* 重量是否稳定 */
} WeightDetectionState_t;

static WeightDetectionState_t weight_state = {
    .current_weight = 0.0f,
    .last_stable_weight = 0.0f,
    .weight_buffer = {0},
    .buffer_index = 0,
    .stable_count = 0,
    .weight_stable = 0
};
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
static void sort_u16_array(uint16_t *data, uint8_t length)
{
  uint8_t i;
  uint8_t j;

  for (i = 0; i < length; i++) {
    for (j = (uint8_t)(i + 1); j < length; j++) {
      if (data[j] < data[i]) {
        uint16_t temp = data[i];
        data[i] = data[j];
        data[j] = temp;
      }
    }
  }
}

static uint32_t cs1237_interpolate_segment_x10(int32_t raw_value,
                                               int32_t raw_lo,
                                               int32_t raw_hi,
                                               uint32_t real_lo_x10,
                                               uint32_t real_hi_x10)
{
  int64_t delta_raw = (int64_t)raw_hi - (int64_t)raw_lo;
  int64_t delta_real = (int64_t)real_hi_x10 - (int64_t)real_lo_x10;
  int64_t numerator;
  int64_t corrected_x10;

  if (delta_raw == 0LL) {
    return real_lo_x10;
  }

  numerator = ((int64_t)raw_value - (int64_t)raw_lo) * delta_real;

  if (numerator >= 0) {
    corrected_x10 = (int64_t)real_lo_x10 + ((numerator + (delta_raw / 2LL)) / delta_raw);
  } else {
    corrected_x10 = (int64_t)real_lo_x10 + ((numerator - (delta_raw / 2LL)) / delta_raw);
  }

  if (corrected_x10 < 0LL) {
    return 0U;
  }

  return (uint32_t)corrected_x10;
}

static uint32_t cs1237_apply_segment_calibration_x10(int32_t raw_value)
{
#if (CS1237_SEGMENT_CAL_ENABLE != 0U)
  static const int32_t raw_points[CS1237_CAL_POINT_COUNT] = {
      CS1237_CAL0_RAW,
      CS1237_CAL1_RAW,
      CS1237_CAL2_RAW,
      CS1237_CAL3_RAW,
      CS1237_CAL4_RAW,
      CS1237_CAL5_RAW
  };
  static const uint32_t real_points[CS1237_CAL_POINT_COUNT] = {
      CS1237_CAL0_REAL_X10,
      CS1237_CAL1_REAL_X10,
      CS1237_CAL2_REAL_X10,
      CS1237_CAL3_REAL_X10,
      CS1237_CAL4_REAL_X10,
      CS1237_CAL5_REAL_X10
  };
  uint8_t i;
  uint8_t best_idx = 0U;
  uint32_t best_distance = 0xFFFFFFFFU;

  if ((raw_points[0] == raw_points[1]) ||
      (raw_points[1] == raw_points[2]) ||
      (raw_points[2] == raw_points[3]) ||
      (raw_points[3] == raw_points[4]) ||
      (raw_points[4] == raw_points[5])) {
    return CS1237_GetMeasurementX10();
  }

  for (i = 0U; i < (CS1237_CAL_POINT_COUNT - 1U); i++) {
    int32_t raw_a = raw_points[i];
    int32_t raw_b = raw_points[i + 1U];
    int32_t range_min = (raw_a < raw_b) ? raw_a : raw_b;
    int32_t range_max = (raw_a > raw_b) ? raw_a : raw_b;

    if ((raw_value >= range_min) && (raw_value <= range_max)) {
      return cs1237_interpolate_segment_x10(raw_value,
                                            raw_a,
                                            raw_b,
                                            real_points[i],
                                            real_points[i + 1U]);
    }
  }

  for (i = 0U; i < (CS1237_CAL_POINT_COUNT - 1U); i++) {
    int32_t raw_a = raw_points[i];
    int32_t raw_b = raw_points[i + 1U];
    uint32_t distance_a = (raw_value > raw_a) ? (uint32_t)(raw_value - raw_a) : (uint32_t)(raw_a - raw_value);
    uint32_t distance_b = (raw_value > raw_b) ? (uint32_t)(raw_value - raw_b) : (uint32_t)(raw_b - raw_value);
    uint32_t segment_distance = (distance_a < distance_b) ? distance_a : distance_b;

    if (segment_distance < best_distance) {
      best_distance = segment_distance;
      best_idx = i;
    }
  }

  return cs1237_interpolate_segment_x10(raw_value,
                                        raw_points[best_idx],
                                        raw_points[best_idx + 1U],
                                        real_points[best_idx],
                                        real_points[best_idx + 1U]);
#else
  return CS1237_GetMeasurementX10();
#endif
}


/**
  * @brief ADC鲁棒滤波：中值/去极值均值 + 自适应指数平滑
  * @param new_sample: 新的ADC采样值 (0~4095)
  * @retval 滤波后的ADC值
  */
uint16_t robust_adc_filter(uint16_t new_sample)
{
  uint16_t sorted[ADC_FILTER_WINDOW];
  uint32_t sum = 0;
  uint8_t i;
  uint8_t start;
  uint8_t end;
  uint8_t count;
  float trimmed_mean;
  float diff;
  float alpha;

  adc_buffer[adc_index] = new_sample;
  if (adc_filled < ADC_FILTER_WINDOW) {
      adc_filled++;
  }
  adc_index = (adc_index + 1) % ADC_FILTER_WINDOW;

  for (i = 0; i < adc_filled; i++) {
    sorted[i] = adc_buffer[i];
  }

  sort_u16_array(sorted, adc_filled);

  if (adc_filled <= (ADC_FILTER_TRIM * 2U)) {
    start = 0;
    end = adc_filled;
  } else {
    start = ADC_FILTER_TRIM;
    end = (uint8_t)(adc_filled - ADC_FILTER_TRIM);
  }

  count = (uint8_t)(end - start);
  for (i = start; i < end; i++) {
    sum += sorted[i];
  }

  if (count == 0) {
    return new_sample;
  }

  trimmed_mean = (float)sum / (float)count;

  if (!adc_ema_initialized) {
    adc_ema = trimmed_mean;
    adc_ema_initialized = 1;
    return (uint16_t)(adc_ema + 0.5f);
  }

  diff = trimmed_mean - adc_ema;
  alpha = (fabsf(diff) > ADC_FILTER_JUMP_THRESHOLD) ?
      ADC_FILTER_ALPHA_FAST : ADC_FILTER_ALPHA_SLOW;
  adc_ema += alpha * diff;

  return (uint16_t)(adc_ema + 0.5f);
}

/**
  * @brief 执行线性回归标定（最小二乘法）
  * @retval 1: 标定成功, 0: 标定失败
  */
uint8_t perform_calibration(void)
{
    float sum_x = 0.0f, sum_y = 0.0f, sum_xy = 0.0f, sum_xx = 0.0f;
    uint8_t i;
    
    /* 检查标定点数量 */
    if (CALIBRATION_POINTS < 2) {
        return 0;
    }
    
    /* 计算统计量 */
    for (i = 0; i < CALIBRATION_POINTS; i++) {
        float x = (float)calibration_points[i].adc_value;
        float y = calibration_points[i].weight_g;
        
        sum_x += x;
        sum_y += y;
        sum_xy += x * y;
        sum_xx += x * x;
    }
    
    float n = (float)CALIBRATION_POINTS;
    float denominator = n * sum_xx - sum_x * sum_x;
    
    /* 防止除零 */
    if (denominator == 0.0f) {
        return 0;
    }
    
    /* 计算斜率和偏移 */
    calibration_slope = (n * sum_xy - sum_x * sum_y) / denominator;
    calibration_offset = (sum_y * sum_xx - sum_x * sum_xy) / denominator;
    
    calibration_valid = 1;
    return 1;
}

/**
  * @brief 根据ADC值计算质量（克）
  * @param adc_value: 滤波后的ADC值
  * @retval 计算出的质量（克），如果标定无效返回0
  */
float calculate_weight_g(uint16_t adc_value)
{
    if (!calibration_valid) {
        return 0.0f;
    }
    
    /* 线性公式：weight = slope * adc + offset */
    float weight = calibration_slope * (float)adc_value + calibration_offset;
    
    /* 限制重量为非负值 */
    if (weight < 0.0f) {
        weight = 0.0f;
    }
    
    return weight;
}

/**
  * @brief 更新标定点数据
  * @param index: 标定点索引（0~CALIBRATION_POINTS-1）
  * @param adc_value: ADC读数
  * @param weight_g: 对应重量（克）
  */
void update_calibration_point(uint8_t index, uint16_t adc_value, float weight_g)
{
    if (index < CALIBRATION_POINTS) {
        calibration_points[index].adc_value = adc_value;
        calibration_points[index].weight_g = weight_g;
        calibration_valid = 0;  /* 标定数据已更新，需要重新标定 */
    }
}

/**
  * @brief 增量检测算法 - 检测重量变化并稳定输出
  * @param raw_weight: 原始计算出的重量（克）
  * @retval 稳定后的重量（克），如果重量变化小于阈值则保持原值
  */
float incremental_weight_detection(float raw_weight)
{
    static uint8_t initialized = 0;
    float avg_weight = 0.0f;
    uint8_t i;
    
    /* 第一次调用时初始化 */
    if (!initialized) {
        weight_state.current_weight = raw_weight;
        weight_state.last_stable_weight = raw_weight;
        for (i = 0; i < STABLE_SAMPLES_REQUIRED; i++) {
            weight_state.weight_buffer[i] = raw_weight;
        }
        initialized = 1;
        return raw_weight;
    }
    
    /* 将新重量存入缓冲区 */
    weight_state.weight_buffer[weight_state.buffer_index] = raw_weight;
    weight_state.buffer_index = (weight_state.buffer_index + 1) % STABLE_SAMPLES_REQUIRED;
    
    /* 计算缓冲区平均值 */
    avg_weight = 0.0f;
    for (i = 0; i < STABLE_SAMPLES_REQUIRED; i++) {
        avg_weight += weight_state.weight_buffer[i];
    }
    avg_weight /= STABLE_SAMPLES_REQUIRED;
    
    /* 计算与当前稳定重量的差值 */
    float weight_diff = avg_weight - weight_state.current_weight;
    
    /* 如果变化超过阈值，开始检测稳定状态 */
    if (fabsf(weight_diff) >= WEIGHT_CHANGE_THRESHOLD) {
        /* 重量变化显著，检查是否稳定 */
        weight_state.stable_count++;
        
        if (weight_state.stable_count >= STABLE_SAMPLES_REQUIRED) {
            /* 重量已稳定在新值 */
            weight_state.last_stable_weight = weight_state.current_weight;
            weight_state.current_weight = avg_weight;
            weight_state.weight_stable = 1;
            weight_state.stable_count = 0;
            
            /* 输出重量变化信息 */
            uint8_t txbuf_info[64] = {0};
            int len_info = snprintf((char*)txbuf_info, sizeof(txbuf_info),
                                  "Weight changed: %.1f g -> %.1f g (Δ%.1f g)\r\n",
                                  weight_state.last_stable_weight,
                                  weight_state.current_weight,
                                  weight_diff);
            if (len_info > 0) {
                //HAL_UART_Transmit(&huart1, txbuf_info, (uint16_t)len_info, 100);
            }
        }
    } else {
        /* 重量变化小于阈值，重置稳定计数 */
        weight_state.stable_count = 0;
        weight_state.weight_stable = 1;  /* 保持稳定状态 */
    }
    
    /* 返回当前稳定重量 */
    return weight_state.current_weight;
}

/**
  * @brief 获取重量变化状态
  * @retval 1: 重量正在变化, 0: 重量稳定
  */
uint8_t is_weight_changing(void)
{
    return (weight_state.stable_count > 0);
}

/**
  * @brief 获取重量变化量
  * @retval 重量变化量（克），正数表示增加，负数表示减少
  */
float get_weight_change(void)
{
    return weight_state.current_weight - weight_state.last_stable_weight;
}
/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_USART1_UART_Init();
  /* USER CODE BEGIN 2 */

  /* 初始化 CS1237，并写入默认增益配置 */
  CS1237_Init();
  CS1237_SetGain(CS1237_GAIN_2);

  /* 设置修正系数，空载时做一次去皮 */
  CS1237_SetScaleFactor(CS1237_SCALE_FACTOR);
  uint8_t txbuf[64] = {0};

  /* 保存 CS1237 原始码值 */
  int32_t raw_value = 0;

  /* 保存换算后的传感器测量值，单位 0.1g */
  uint32_t adjusted_value_x10 = 0;
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */
    /* 读取一次 CS1237 24bit 原始码值 */
    raw_value = CS1237_ReadRawSigned();

    /* 按标定系数换算传感器测量值 */
    adjusted_value_x10 = cs1237_apply_segment_calibration_x10(raw_value);

    /* 通过 USART1 输出原始值和最终重量 */
    int len = snprintf((char*)txbuf, sizeof(txbuf),
                      "raw:%ld weight:%lu g\r\n",
                      raw_value,
                      (adjusted_value_x10 + 5U) / 10U);
    if (len > 0) {
      HAL_UART_Transmit(&huart1, txbuf, (uint16_t)len, 100);
    }

    /* 采样周期 200ms，可根据需要调整 */
    HAL_Delay(200);

    /* USER CODE BEGIN 3 */
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};
  RCC_PeriphCLKInitTypeDef PeriphClkInit = {0};

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI|RCC_OSCILLATORTYPE_HSI14;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSI14State = RCC_HSI14_ON;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.HSI14CalibrationValue = 16;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_NONE;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_HSI;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_0) != HAL_OK)
  {
    Error_Handler();
  }
  PeriphClkInit.PeriphClockSelection = RCC_PERIPHCLK_USART1;
  PeriphClkInit.Usart1ClockSelection = RCC_USART1CLKSOURCE_PCLK1;
  if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInit) != HAL_OK)
  {
    Error_Handler();
  }
}

/* USER CODE BEGIN 4 */
/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}

#ifdef  USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
