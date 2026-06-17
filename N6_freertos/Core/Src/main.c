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

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h" 
#include "semphr.h"
#include <stdio.h>
#include <string.h>
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */
// typedef struct {
//     uint32_t id;        // 第几次发送
//     uint8_t  led_state; // 翻转后 LED 电平
// } LedMsg_t;

// QueueHandle_t xLedQueue;   // 队列句柄(全局, 两个任务都要访问)
extern UART_HandleTypeDef huart1;

SemaphoreHandle_t xRxSem;   // 二值信号量: 中断通知任务"收到字符了"
uint8_t g_rx_byte;          // 中断接收一个字节存这

SemaphoreHandle_t xUartMutex; 
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */
void vLedTask(void *pvParameters);
// void vUartTask(void *pvParameters);
void vCmdTask(void *pvParameters);
void uart_print(const char *str);
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

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
  // xLedQueue = xQueueCreate(5, sizeof(LedMsg_t));   // 容量5个元素, 每个 sizeof(LedMsg_t)
  // if (xLedQueue == NULL) {
  //     Error_Handler();   // 堆不足, 创建失败
  // }

  xRxSem = xSemaphoreCreateBinary();
  if (xRxSem == NULL) { Error_Handler(); }

  xUartMutex = xSemaphoreCreateMutex();           // 创建互斥量
  if (xUartMutex == NULL) { Error_Handler(); }

  HAL_UART_Receive_IT(&huart1, &g_rx_byte, 1);

  xTaskCreate(vLedTask, "LED",  configMINIMAL_STACK_SIZE * 2,     NULL, 1, NULL);
  xTaskCreate(vCmdTask, "CMD", configMINIMAL_STACK_SIZE * 2, NULL, 2, NULL);
  // xTaskCreate(vUartTask, "UART", configMINIMAL_STACK_SIZE * 2, NULL, 1, NULL);
  vTaskStartScheduler();          // 启动调度器! 此行之后 CPU 交给 FreeRTOS
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

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

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.HSEPredivValue = RCC_HSE_PREDIV_DIV1;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLMUL = RCC_PLL_MUL9;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)
  {
    Error_Handler();
  }
}

/* USER CODE BEGIN 4 */
void vLedTask(void *pvParameters)
{
    char buf[32];
    for (;;)
    {
        HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_13);

        uint8_t state = HAL_GPIO_ReadPin(GPIOC, GPIO_PIN_13);
        snprintf(buf, sizeof(buf), "LED state: %u\r\n", state);
        uart_print(buf);                        // 通过互斥量保护打印

        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

// void vUartTask(void *pvParameters)
// {
//     LedMsg_t rx;
//     char buf[64];

//     for (;;)
//     {
//         // 阻塞等待队列数据, 无限等待 —— 没数据就睡, 不空转
//         if (xQueueReceive(xLedQueue, &rx, portMAX_DELAY) == pdPASS)
//         {
//             int len = snprintf(buf, sizeof(buf),
//                                "msg id=%lu, led=%u\r\n",
//                                (unsigned long)rx.id, rx.led_state);
//             HAL_UART_Transmit(&huart1, (uint8_t *)buf, len, HAL_MAX_DELAY);
//         }
//     }
// }

void vCmdTask(void *pvParameters)
{
    char buf[48];
    for (;;)
    {
        if (xSemaphoreTake(xRxSem, portMAX_DELAY) == pdTRUE)
        {
            snprintf(buf, sizeof(buf), "RX: 0x%02X ('%c')\r\n", g_rx_byte, g_rx_byte);
            uart_print(buf);                    // 同一把锁保护
        }
    }
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART1)
    {
        BaseType_t xHigherPriorityTaskWoken = pdFALSE;

        // 唤醒处理任务, 回填"是否唤醒了更高优任务"
        xSemaphoreGiveFromISR(xRxSem, &xHigherPriorityTaskWoken);

        // 立即重新启动下一次接收中断(否则只能收一个字符)
        HAL_UART_Receive_IT(&huart1, &g_rx_byte, 1);

        // 若唤醒了更高优任务, 中断退出时立即切换
        portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
    }
}

// 线程安全的串口打印: 加锁 -> 发送 -> 解锁
void uart_print(const char *str)
{
    xSemaphoreTake(xUartMutex, portMAX_DELAY);          // 拿锁(被占则阻塞等)
    HAL_UART_Transmit(&huart1, (uint8_t *)str, strlen(str), HAL_MAX_DELAY);
    xSemaphoreGive(xUartMutex);                          // 还锁
}
/* USER CODE END 4 */

/**
  * @brief  Period elapsed callback in non blocking mode
  * @note   This function is called  when TIM4 interrupt took place, inside
  * HAL_TIM_IRQHandler(). It makes a direct call to HAL_IncTick() to increment
  * a global variable "uwTick" used as application time base.
  * @param  htim : TIM handle
  * @retval None
  */
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
  /* USER CODE BEGIN Callback 0 */

  /* USER CODE END Callback 0 */
  if (htim->Instance == TIM4)
  {
    HAL_IncTick();
  }
  /* USER CODE BEGIN Callback 1 */

  /* USER CODE END Callback 1 */
}

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
#ifdef USE_FULL_ASSERT
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
