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
#include "pressure_debug_uart.h"
#include "protocol.h"
#include "pressure_threshold_store.h"
#include "weight_calibration.h"
#include "pressure_auto_zero.h"
#include "calibration_protocol.h"


/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
/* 压力值下降滤波分母：堵管停泵后低值抖动按 1/4 逐帧回落，避免压力显示和闭环输入瞬间掉到低值。 */
#define PRESSURE_FILTER_FALL_DIV        4U

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */
/* 当前压力阈值，单位 g，默认使用宏定义值 */
static uint16_t pressure_threshold_g = PRESSURE_THRESHOLD_DEFAULT_G;
static uint8_t pressure_filter_ready = 0U;
static uint32_t pressure_filtered_x10 = 0U;
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);

/* USER CODE BEGIN 0 */
/*
 * 函数功能：对标定后的压力值做跨帧滤波，压力上升立即跟随，压力下降按比例缓慢回落。
 * 输入参数：current_x10 当前帧标定后的压力值，单位 0.1g。
 * 返回参数：滤波后的压力值，单位 0.1g，用于串口上报和主控闭环判断。
 */
static uint32_t PressureFilter_ApplyX10(uint32_t current_x10)
{
  /* 保存当前帧与滤波值之间的下降差值，单位 0.1g。 */
  uint32_t drop_x10;

  /* 首帧直接采用当前压力，保证上电后显示和闭环输入没有人为延迟。 */
  if (pressure_filter_ready == 0U) {
    /* 标记滤波器已经初始化，后续帧才进入上升/下降分支。 */
    pressure_filter_ready = 1U;
    /* 首帧作为滤波基准，避免默认 0 参与压力计算。 */
    pressure_filtered_x10 = current_x10;
    /* 返回首帧压力，保持启动阶段行为直观。 */
    return pressure_filtered_x10;
  }

  /* 压力上升直接跟随，确保堵管压力升高时主控能及时触发保护。 */
  if (current_x10 >= pressure_filtered_x10) {
    /* 当前值更高时立即刷新滤波值，不延迟保护阈值判断。 */
    pressure_filtered_x10 = current_x10;
    /* 返回最新高值，让上报压力和闭环输入同步抬升。 */
    return pressure_filtered_x10;
  }

  /* 压力下降时计算与上一帧滤波值的差值，用于分段回落。 */
  drop_x10 = pressure_filtered_x10 - current_x10;
  /* 下降只回落差值的 1/PRESSURE_FILTER_FALL_DIV，抑制停泵后的低值跳变。 */
  pressure_filtered_x10 -=
      (drop_x10 + PRESSURE_FILTER_FALL_DIV - 1U) / PRESSURE_FILTER_FALL_DIV;

  /* 返回慢速回落后的压力，避免主控收到忽高忽低的压力输入。 */
  return pressure_filtered_x10;
}

static void PressureFilter_Reset(void)
{
  pressure_filter_ready = 0U;
  pressure_filtered_x10 = 0U;
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

#if (PRESSURE_UART_DEBUG_TEXT_ENABLE != 0U)
  char debug_txbuf[PRESSURE_UART_DEBUG_TEXT_BUF_SIZE] = {0};
#else
  uint8_t txbuf[CS1237_UART_PROTOCOL_FRAME_SIZE] = {0};
  uint8_t tx_seq = 0U;
#endif

  /* 保存 CS1237 原始码值 */
  int32_t raw_value = 0;

  /* 保存换算后的传感器测量值，单位 0.1g */
  uint32_t adjusted_value_x10 = 0;
  uint8_t sample_valid = 0U;
  uint8_t device_code = 0U;

  /* 启动时读取阈值；若无有效数据则使用默认值 */
  if (PressureThreshold_Load(&pressure_threshold_g) == 0U) {
    pressure_threshold_g = PRESSURE_THRESHOLD_DEFAULT_G;
  }
  PressureThreshold_SetRuntime(pressure_threshold_g);
  PressureAutoZero_Init();
  CalibrationProtocol_Init(&huart1);

  /* 可选：启动时强制写入宏定义阈值，常用于首次配置 */
#if (PRESSURE_THRESHOLD_FLASH_STORE_ENABLE != 0U) && (PRESSURE_THRESHOLD_FORCE_WRITE_ON_BOOT != 0U)
  pressure_threshold_g = (uint16_t)PRESSURE_THRESHOLD_FORCE_VALUE_G;
  (void)PressureThreshold_Save(pressure_threshold_g);
  PressureThreshold_SetRuntime(pressure_threshold_g);
#endif
  /* USER CODE END 2 */
  /* 对称去抖：计数器和稳定状态（1=高，0=低），默认上拉为高 */
  uint8_t cnt1 = 0, cnt2 = 0, cnt3 = 0, cnt4 = 0;
  uint8_t st1 = 1, st2 = 1, st3 = 1, st4 = 1;
  const uint8_t TH = 2;
  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */
    CalibrationProtocol_Process(&huart1);
    /* 读取 CS1237 中值 raw，上传帧格式不变，仅抑制单次尖峰。 */
    sample_valid = CS1237_ReadMedianChecked(0U, &raw_value);
    if (sample_valid == 0U) {
      raw_value = 0;
    }

    /* 原始读数：SET=高(1)，RESET=低(0) */
    uint8_t r1 = (HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_1) == GPIO_PIN_SET) ? 1 : 0;
    uint8_t r2 = (HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_2) == GPIO_PIN_SET) ? 1 : 0;
    uint8_t r3 = (HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_3) == GPIO_PIN_SET) ? 1 : 0;
    uint8_t r4 = (HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_4) == GPIO_PIN_SET) ? 1 : 0;

    /* 对称去抖：需要 TH 次连续样本才改变稳定状态 */
    if (r1 != st1) { if (cnt1 < TH) cnt1++; if (cnt1 >= TH) { st1 = r1; cnt1 = 0; } } else { cnt1 = 0; }
    if (r2 != st2) { if (cnt2 < TH) cnt2++; if (cnt2 >= TH) { st2 = r2; cnt2 = 0; } } else { cnt2 = 0; }
    if (r3 != st3) { if (cnt3 < TH) cnt3++; if (cnt3 >= TH) { st3 = r3; cnt3 = 0; } } else { cnt3 = 0; }
    if (r4 != st4) { if (cnt4 < TH) cnt4++; if (cnt4 >= TH) { st4 = r4; cnt4 = 0; } } else { cnt4 = 0; }

    device_code = CS1237UartProtocol_PackDeviceCode(st1, st2, st3, st4);
    PressureAutoZero_Update(raw_value, sample_valid, device_code);
    if ((sample_valid != 0U) && (PressureAutoZero_IsReady() != 0U)) {
      int32_t boot_empty_raw = PressureAutoZero_GetBootEmptyRaw();
      int32_t current_from_empty_raw = raw_value - boot_empty_raw;
      int32_t session_zero_from_empty_raw =
          PressureAutoZero_GetSessionZeroRaw() - boot_empty_raw;
      adjusted_value_x10 = WeightCalibration_ApplySessionX10(
          current_from_empty_raw,
          session_zero_from_empty_raw);
      adjusted_value_x10 = PressureFilter_ApplyX10(adjusted_value_x10);
    } else {
      PressureFilter_Reset();
      adjusted_value_x10 = PRESSURE_AUTO_ZERO_FAILSAFE_X10;
    }

    /* 通过 USART1 输出原始值和最终重量以及st1、st2、st3、st4 */
    {
#if (PRESSURE_UART_DEBUG_TEXT_ENABLE != 0U)
      uint16_t debug_len = PressureDebugUart_FormatSampleLine(debug_txbuf,
                                                              sizeof(debug_txbuf),
                                                              raw_value,
                                                              adjusted_value_x10);

      if (debug_len > 0U) {
        (void)HAL_UART_Transmit(&huart1, (uint8_t *)debug_txbuf, debug_len, 100);
      }
#else
      uint16_t frame_len = CS1237UartProtocol_BuildReportFrame(txbuf,
                                                               sizeof(txbuf),
                                                               tx_seq,
                                                               raw_value,
                                                               adjusted_value_x10,
                                                               PressureThreshold_GetRuntime(),
                                                               device_code);

      if (frame_len > 0U) {
        if (HAL_UART_Transmit(&huart1, txbuf, frame_len, 100) == HAL_OK) {
          tx_seq++;
        }
      }
#endif
    }
    /* 采样周期 200ms，可根据需要调整 */
    HAL_Delay(200);
    CalibrationProtocol_Process(&huart1);

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
