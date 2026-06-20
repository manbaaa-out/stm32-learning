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
#include "i2c.h"
#include "usart.h"
#include "gpio.h"


/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h" 
#include "semphr.h"
#include "bh1750.h"
#include "dht11.h"
#include "stream_buffer.h"
#include "frame_parser.h"
#include "crc16.h"
#include <stdio.h>
#include <string.h>
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */
typedef struct{
  uint8_t data[69];
  uint8_t len;
} TxFrame_t;

typedef struct {
  float temperature;
  float humidity;
  float lux;
  uint8_t lux_valid;
  uint8_t th_valid;
} SensorData_t;
/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define HEARTBEAT_MS  1000U
#define RX_STREAM_SIZE  64
#define RX_DMA_SIZE     64
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN PV */
QueueHandle_t xTxQueue;
static SemaphoreHandle_t g_sensor_mutex;
static volatile uint32_t g_sample_period_ms = 5UL*60*1000;
static StreamBufferHandle_t g_rx_stream;
static uint8_t  g_rx_dma_buf[RX_DMA_SIZE];
static uint16_t g_rx_old_pos = 0;

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */
void vTxTask(void* pvParameters);
void vSampleReportTask(void *pv);
void vCmdTask(void *pv);

static uint8_t frame_build(uint8_t type, const uint8_t *payload, uint8_t payload_len, uint8_t *out);
static void sample_all(SensorData_t *out);
static void send_heartbeat(void);
static void report_frame(const SensorData_t *s);
static void send_resp(uint8_t type, const uint8_t *payload, uint8_t len, uint8_t seq);
static void on_frame(uint8_t type, const uint8_t *payload, uint8_t len);
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
  MX_I2C1_Init();
  /* USER CODE BEGIN 2 */
  xTxQueue = xQueueCreate(8, sizeof(TxFrame_t));
  if (xTxQueue == NULL) {Error_Handler();}

  g_sensor_mutex = xSemaphoreCreateMutex();
  if (g_sensor_mutex == NULL) { Error_Handler(); }

  g_rx_stream = xStreamBufferCreate(RX_STREAM_SIZE, 1);   // 水位 1: 来一字节就唤醒
  if (g_rx_stream == NULL) { Error_Handler(); }

  /* 创建任务并检查返回值: 堆不够(configTOTAL_HEAP_SIZE 太小)时会在此处停住,
     而不是任务静默创建失败、之后行为诡异。优先级: 命令(3) > TX(2) > 采样上报(1) */
  if (xTaskCreate(vTxTask, "Trans", configMINIMAL_STACK_SIZE * 2, NULL, 2, NULL) != pdPASS) { Error_Handler(); }
  if (xTaskCreate(vSampleReportTask, "Sample", configMINIMAL_STACK_SIZE * 3, NULL, 1, NULL) != pdPASS) { Error_Handler(); }
  if (xTaskCreate(vCmdTask, "Cmd", configMINIMAL_STACK_SIZE * 3, NULL, 3, NULL) != pdPASS) { Error_Handler(); }

  vTaskStartScheduler();
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
void vTxTask(void* pvParameters) {
  TxFrame_t frame;
  while(1) {
    if (xQueueReceive(xTxQueue, &frame, portMAX_DELAY) == pdPASS) {
      HAL_UART_Transmit(&huart1, frame.data, frame.len, HAL_MAX_DELAY);
    }
  }
}

/* ---- 命令任务私有状态: 上次处理过的 seq + 当时的应答(§6.2 幂等)----
   只有 on_frame(命令任务上下文)碰它们, 单线程, 无需加锁。 */
static uint8_t   g_last_seq      = 0;
static uint8_t   g_have_last_seq = 0;     /* 开机还没处理过任何命令 */
static TxFrame_t g_last_resp;

/* 组一帧应答投 TX 队列, 并存档供同 seq 重发时复发 */
static void send_resp(uint8_t type, const uint8_t *payload, uint8_t len, uint8_t seq)
{
    TxFrame_t f;
    f.len = frame_build(type, payload, len, f.data);
    xQueueSend(xTxQueue, &f, 0);
    g_last_resp = f;  g_last_seq = seq;  g_have_last_seq = 1;
}

/* 拼齐且 CRC 已校验的整帧到这。payload[0] = seq(§6.2) */
static void on_frame(uint8_t type, const uint8_t *payload, uint8_t len)
{
    if (len < 1) return;                  /* 无 payload 的帧无法应答, 丢 */
    uint8_t seq = payload[0];

    /* §6.2 幂等: 与上次 seq 相同 → 判重发, 不重复执行命令本体, 只补发上次应答 */
    if (g_have_last_seq && seq == g_last_seq) {
        xQueueSend(xTxQueue, &g_last_resp, 0);
        return;
    }

    switch (type) {
    case 0x20: {                          /* 查光照 → 现采 → 0x05 [seq][rc][光照2B] */
        SensorData_t s; sample_all(&s);
        if (s.lux_valid) {
            uint16_t lx = (uint16_t)(s.lux + 0.5f);
            uint8_t p[4] = { seq, 0x00, (uint8_t)(lx >> 8), (uint8_t)(lx & 0xFF) };
            send_resp(0x05, p, 4, seq);
        } else {
            uint8_t p[2] = { seq, 0x03 };          /* 0x03 暂不可用(读失败) */
            send_resp(0x05, p, 2, seq);
        }
        break;
    }
    case 0x21: {                          /* 查温湿度 → 现采 → 0x05 [seq][rc][温×10 2B][湿×10 2B] */
        SensorData_t s; sample_all(&s);
        if (s.th_valid) {
            uint16_t t = (uint16_t)(s.temperature * 10.0f + 0.5f);
            uint16_t h = (uint16_t)(s.humidity    * 10.0f + 0.5f);
            uint8_t p[6] = { seq, 0x00,
                             (uint8_t)(t >> 8), (uint8_t)(t & 0xFF),
                             (uint8_t)(h >> 8), (uint8_t)(h & 0xFF) };
            send_resp(0x05, p, 6, seq);
        } else {
            uint8_t p[2] = { seq, 0x03 };
            send_resp(0x05, p, 2, seq);
        }
        break;
    }
    case 0x22: {                          /* 设周期(秒)→ 校验 → 写 g_sample_period_ms → 0x06 [seq][rc] */
        uint8_t rc;
        if (len < 3) {
            rc = 0x01;                              /* 缺周期字节 → 参数非法 */
        } else {
            uint16_t period_s = (uint16_t)((payload[1] << 8) | payload[2]);
            if (period_s == 0) {
                rc = 0x01;                          /* §6.3: 周期=0 参数非法 */
            } else {
                g_sample_period_ms = (uint32_t)period_s * 1000U;   /* 秒 → ms */
                rc = 0x00;
            }
        }
        uint8_t p[2] = { seq, rc };
        send_resp(0x06, p, 2, seq);
        break;
    }
    default: {                            /* §6.3: 未知下行 / 收到上行 TYPE → rc=0x02 不支持 */
        uint8_t p[2] = { seq, 0x02 };
        send_resp(0x06, p, 2, seq);
        break;
    }
    }
}

void vCmdTask(void *pv)
{
    (void)pv;
    FrameParser parser;
    frame_parser_init(&parser, on_frame);

    HAL_UARTEx_ReceiveToIdle_DMA(&huart1, g_rx_dma_buf, RX_DMA_SIZE);  // 启动循环 DMA 接收

    uint8_t buf[32];
    for (;;) {
        size_t n = xStreamBufferReceive(g_rx_stream, buf, sizeof buf, portMAX_DELAY);
        for (size_t i = 0; i < n; i++)
            frame_parser_feed(&parser, buf[i]);
    }
}

static uint8_t frame_build(uint8_t type, const uint8_t *payload,
                           uint8_t payload_len, uint8_t *out)
{
    uint8_t n = 0;
    out[n++] = 0xAA;
    out[n++] = 0x55;
    out[n++] = (uint8_t)(1 + payload_len);            // LEN
    out[n++] = type;                                  // TYPE
    for (uint8_t i = 0; i < payload_len; i++)
        out[n++] = payload[i];                        // payload

    uint16_t crc = 0xFFFF;                            // 复用你的 crc16_update
    for (uint8_t i = 2; i < n; i++) {                 // out[2]=LEN 起, 覆盖 LEN+TYPE+payload
        crc = crc16_update(crc, out[i]);              
    }


    out[n++] = (uint8_t)(crc & 0xFF);                 // CRC_LO(小端)
    out[n++] = (uint8_t)(crc >> 8);                   // CRC_HI
    return n;
}

static void sample_all(SensorData_t *out)
{
    SensorData_t s = {0};                              // 本次局部快照, 两 valid 默认 0

    xSemaphoreTake(g_sensor_mutex, portMAX_DELAY);
    float   lux = BH1750_ReadLux();
    uint8_t dht[5];
    uint8_t ok  = DHT11_Read(dht);
    xSemaphoreGive(g_sensor_mutex);                    // 读完立刻放锁, 解析在锁外

    if (ok == 0)     { s.temperature = dht[2]; s.humidity = dht[0]; s.th_valid = 1; }
    if (lux >= 0.0f) { s.lux = lux; s.lux_valid = 1; }
    *out = s;
}

static void send_heartbeat(void)
{
    TxFrame_t f;
    f.len = frame_build(0x03, NULL, 0, f.data);        // NULL+0: 内部循环零次, 安全
    xQueueSend(xTxQueue, &f, 0);                        // 满则丢
}

static void report_frame(const SensorData_t *s)
{
    TxFrame_t f;

    if (s->th_valid) {
        uint16_t t = (uint16_t)(s->temperature * 10.0f + 0.5f);  // +0.5f: 截断前四舍五入
        uint16_t h = (uint16_t)(s->humidity    * 10.0f + 0.5f);
        uint8_t  p[5];
        p[0] = (uint8_t)(t >> 8);  p[1] = (uint8_t)(t & 0xFF);   // 温度 大端
        p[2] = (uint8_t)(h >> 8);  p[3] = (uint8_t)(h & 0xFF);   // 湿度 大端
        p[4] = (uint8_t)(p[0] + p[1] + p[2] + p[3]);             // 校验位: 网关只验 CRC16, 此字节不解析(占位)
        f.len = frame_build(0x01, p, 5, f.data);
        xQueueSend(xTxQueue, &f, 0);
    }

    if (s->lux_valid) {
        float lf = s->lux;
        if (lf < 0.0f)     lf = 0.0f;
        if (lf > 65535.0f) lf = 65535.0f;              // u16 封顶
        uint16_t lx = (uint16_t)(lf + 0.5f);           // 400.x -> 400
        uint8_t  p[2];
        p[0] = (uint8_t)(lx >> 8);  p[1] = (uint8_t)(lx & 0xFF); // 光照 大端
        f.len = frame_build(0x02, p, 2, f.data);
        xQueueSend(xTxQueue, &f, 0);
    }
    
    /* 0x04 设备状态: 无条件发, 跟随采样轮, 复用本轮 valid 标志 (§3.3 TYPE 字典)
    bit0=DHT11 OK, bit1=BH1750 OK, bit2~7 预留=0
    即便上面两帧都没发(传感器全挂), 这帧也要出去, 让网关知道"节点在, 但外设故障" */
    {
        uint8_t st = (uint8_t)((s->th_valid ? 0x01 : 0x00) |
                               (s->lux_valid ? 0x02 : 0x00));
        f.len = frame_build(0x04, &st, 1, f.data);
        xQueueSend(xTxQueue, &f, 0);
    }
}

void vSampleReportTask(void *pv)
{
    (void)pv;
    TickType_t last_wake = xTaskGetTickCount();   // 基准时刻
    uint32_t   since_sample_ms = 0xFFFFFFFFU;     // 置满, 开机第一轮就采

    for (;;) {
        send_heartbeat();                          // 每个 1s 节拍发心跳

        if (since_sample_ms >= g_sample_period_ms) {
            SensorData_t s;
            sample_all(&s);
            report_frame(&s);
            since_sample_ms = 0;
        }

        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(HEARTBEAT_MS));  // 绝对 1s 栅格
        since_sample_ms += HEARTBEAT_MS;
    }
}

void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *huart, uint16_t Size)
{
    if (huart->Instance != USART1) return;

    BaseType_t woken = pdFALSE;
    if (Size != g_rx_old_pos) {
        if (Size > g_rx_old_pos) {                         // 未回绕: [old_pos, Size)
            xStreamBufferSendFromISR(g_rx_stream,
                &g_rx_dma_buf[g_rx_old_pos], Size - g_rx_old_pos, &woken);
        } else {                                           // 回绕: [old_pos, 末尾) + [0, Size)
            xStreamBufferSendFromISR(g_rx_stream,
                &g_rx_dma_buf[g_rx_old_pos], RX_DMA_SIZE - g_rx_old_pos, &woken);
            if (Size > 0)
                xStreamBufferSendFromISR(g_rx_stream,
                    &g_rx_dma_buf[0], Size, &woken);
        }
        g_rx_old_pos = Size;
        if (g_rx_old_pos >= RX_DMA_SIZE) g_rx_old_pos = 0;
    }
    portYIELD_FROM_ISR(woken);
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