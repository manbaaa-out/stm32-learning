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
#include "dma.h"
#include "usart.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <stdint.h>
#include "frame_parser.h"
#include "crc16.h"
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
#define RX_BUFFER_SIZE 64
uint8_t rxBuffer[RX_BUFFER_SIZE];
uint16_t oldPos = 0;
static FrameParser g_parser;

volatile uint32_t g_frame_count = 0;
volatile uint8_t  g_last_type   = 0;

// 中断与主循环协作:中断登记命令,主循环执行
volatile uint8_t g_cmd_pending = 0;
volatile uint8_t g_cmd_type    = 0;
volatile uint8_t g_cmd_seq     = 0;
volatile uint8_t g_cmd_arg[8];
volatile uint8_t g_cmd_arg_len = 0;

static void on_frame(uint8_t type, const uint8_t *payload, uint8_t len) {
    g_frame_count++;
    g_last_type = type;
    // 中断上下文:只登记,不执行(I/O 上下文不阻塞铁律的 MCU 版)
    if (g_cmd_pending) return;                       // 上条没处理完,丢弃(最简策略)
    g_cmd_type = type;
    g_cmd_seq  = (len >= 1) ? payload[0] : 0;        // payload[0] = seq
    g_cmd_arg_len = (len >= 1) ? (uint8_t)(len - 1) : 0;
    for (uint8_t i = 0; i < g_cmd_arg_len && i < sizeof(g_cmd_arg); i++)
        g_cmd_arg[i] = payload[1 + i];
    g_cmd_pending = 1;                               // 置标志,主循环看到
}

static void send_frame(uint8_t type, const uint8_t *payload, uint8_t payload_len) {
    uint8_t frame[69];   // 单帧最大 69 字节(协议 §1.4)
    uint8_t idx = 0;

    // TODO 你填:
    // 1. 帧头 0xAA 0x55
    frame[idx++] = 0xAA;
    frame[idx++] = 0x55;
    // 2. LEN = 1(TYPE) + payload_len
    frame[idx++] = 1 + payload_len;
    // 3. TYPE
    frame[idx++] = type;
    // 4. payload 各字节
    for (int i = 0; i < payload_len; i++) {
      frame[idx++] = *(payload+i);
    }
    // 5. CRC:计算范围是 [LEN, TYPE, payload...](协议 §2.3,不含帧头不含CRC)
    //    提示:用 crc16(指向 LEN 那个字节, 1+1+payload_len)
    //    然后小端写入:先 CRC_LO(低字节) 再 CRC_HI(高字节)(协议 §4.4)
    uint16_t crc = crc16(&frame[2], 2 + payload_len);
    frame[idx++] = (uint8_t)(crc & 0xFF);
    frame[idx++] = (uint8_t)(crc >> 8);
    // 6. HAL_UART_Transmit(&huart1, frame, idx, 超时) 发出去
    HAL_UART_Transmit(&huart1, frame, idx, 100);

}


// 结果码(协议 §6.3)
#define RC_OK            0x00
#define RC_BAD_PARAM     0x01
#define RC_UNSUPPORTED   0x02

static void handle_command(uint8_t type, uint8_t seq, const uint8_t *arg, uint8_t arg_len) {
    switch (type) {
    case 0x20: {  // 查询光照 -> 回 0x05 查询应答(带数据)
        uint16_t lux = 0x0190;  // 假值 400 lux
        uint8_t payload[4];
        payload[0] = seq;
        payload[1] = RC_OK;
        payload[2] = (uint8_t)(lux >> 8);    // 高字节(大端)
        payload[3] = (uint8_t)(lux & 0xFF);  // 低字节
        send_frame(0x05, payload, 4);
        break;
    }
    case 0x21: {  // 查询温湿度 -> 回 0x05(带数据),假值
        uint16_t temp = 0x00FA;  // 假值 25.0℃,×10 定点
        uint16_t humi = 0x01E0;  // 假值 48.0%,×10 定点
        uint8_t payload[6];
        payload[0] = seq;
        payload[1] = RC_OK;
        payload[2] = (uint8_t)(temp >> 8);
        payload[3] = (uint8_t)(temp & 0xFF);
        payload[4] = (uint8_t)(humi >> 8);
        payload[5] = (uint8_t)(humi & 0xFF);
        send_frame(0x05, payload, 6);
        break;
    }
    case 0x22: {  // 设采样周期 -> 回 0x06 ACK
        uint8_t payload[2];
        payload[0] = seq;
        // Fail Fast:参数长度不对 或 周期为 0 → 拒绝
        if (arg_len < 2) {
            payload[1] = RC_BAD_PARAM;
        } else {
            uint16_t period = ((uint16_t)arg[0] << 8) | arg[1];  // 大端
            if (period == 0) {
                payload[1] = RC_BAD_PARAM;
            } else {
                // TODO 后面接真正改采样周期
                payload[1] = RC_OK;
            }
        }
        send_frame(0x06, payload, 2);
        break;
    }
    default: {  // 不支持的命令(含收到上行 TYPE)-> 回 0x06 ACK,RC_UNSUPPORTED
        uint8_t payload[2];
        payload[0] = seq;
        payload[1] = RC_UNSUPPORTED;
        send_frame(0x06, payload, 2);
        break;
    }
    }
}
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */

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
  MX_DMA_Init();
  MX_USART1_UART_Init();
  /* USER CODE BEGIN 2 */
  frame_parser_init(&g_parser, on_frame);
  HAL_UARTEx_ReceiveToIdle_DMA(&huart1, rxBuffer, RX_BUFFER_SIZE);
  __HAL_DMA_DISABLE_IT(&hdma_usart1_rx, DMA_IT_HT);  // 关掉半传输中断
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
    if (g_cmd_pending) {
      handle_command(g_cmd_type, g_cmd_seq, (uint8_t*)g_cmd_arg, g_cmd_arg_len);
      g_cmd_pending = 0;        // 处理完清标志
    }
    HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_13);
    HAL_Delay(100);
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
void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *huart, uint16_t Size)
{
  if (huart->Instance == USART1)
  {
    // Size 是"从 buffer 头到当前写入位置"的累计长度，即新的位置 newPos
    uint16_t newPos = Size;

    if (newPos != oldPos)   // 有新数据
    {
      if (newPos > oldPos)
      {
        // 正常情况：新数据是连续的一段 rxBuffer[oldPos .. newPos-1]
        for (int i = oldPos ; i < newPos; i++) {
          frame_parser_feed(&g_parser, rxBuffer[i]);
        }
      }
      else
      {
        // 绕回情况：DMA 写到 buffer 末尾又回到了开头，分两段发
        // 第一段：oldPos 到 buffer 末尾
        for (int i = oldPos ; i < RX_BUFFER_SIZE; i++) {
          frame_parser_feed(&g_parser, rxBuffer[i]);
        }
        // 第二段：buffer 开头到 newPos
        if (newPos > 0)
          for (int i = 0 ; i < newPos; i++) {
            frame_parser_feed(&g_parser, rxBuffer[i]);
          }
      }
      oldPos = newPos;   // 更新处理位置
    }

    // 重新挂载（Circular 模式其实会自动继续，但 HAL 的事件接收需要重新 arm）
    HAL_UARTEx_ReceiveToIdle_DMA(&huart1, rxBuffer, RX_BUFFER_SIZE);
    __HAL_DMA_DISABLE_IT(&hdma_usart1_rx, DMA_IT_HT);
  }
}
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
