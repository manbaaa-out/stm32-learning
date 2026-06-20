#include "dht11.h"
#include "dwt_delay.h"
#include "FreeRTOS.h"   /* 必须在 task.h 之前: 它带入 FreeRTOSConfig.h + portmacro.h */
#include "task.h"

#define CPU_FREQ_MHZ  72U   /* 与 dwt_delay.c 保持一致, 用于超时换算 */

/* ===== 方向切换零件 ===== */

/* 把 DATA 引脚配成【开漏输出】: 只主动拉低, 输出高时松手靠外部上拉回高。
   单总线必须开漏 + 上拉, 实现"线与", 杜绝多设备同时驱动的总线竞争。 */
static void dht11_set_output(void)
{
    GPIO_InitTypeDef gpio = {0};
    gpio.Pin   = DHT11_Pin;
    gpio.Mode  = GPIO_MODE_OUTPUT_OD;     /* OD = Open-Drain 开漏 */
    gpio.Pull  = GPIO_NOPULL;             /* 模块自带上拉, 不用内部上拉 */
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(DHT11_GPIO_Port, &gpio);
}

/* 把 DATA 引脚配成【输入】: 既"释放总线"(松手, 上拉拉回高), 又能读 DHT11 电平。 */
static void dht11_set_input(void)
{
    GPIO_InitTypeDef gpio = {0};
    gpio.Pin  = DHT11_Pin;
    gpio.Mode = GPIO_MODE_INPUT;
    gpio.Pull = GPIO_NOPULL;              /* 同样靠模块外部上拉 */
    HAL_GPIO_Init(DHT11_GPIO_Port, &gpio);
}

/* ===== 等翻转 + 超时 零件 =====
   等待 DATA 引脚变成 target 电平, 最多等 timeout_us 微秒。
   返回 0=等到了  1=超时(防止传感器掉线导致死循环卡死)。 */
static uint8_t dht11_wait_level(GPIO_PinState target, uint32_t timeout_us)
{
    uint32_t start = DWT->CYCCNT;
    uint32_t ticks = timeout_us * CPU_FREQ_MHZ;

    while (HAL_GPIO_ReadPin(DHT11_GPIO_Port, DHT11_Pin) != target)
    {
        if ((DWT->CYCCNT - start) > ticks)
        {
            return 1;   /* 超时, 放弃 */
        }
    }
    return 0;           /* 等到了目标电平 */
}

/* ===== 主函数: 完整四段时序 ===== */
uint8_t DHT11_Read(uint8_t *data)
{
    uint8_t i, j;
    uint8_t byte;
    uint8_t ret = 0;

    /* ---- (1) 主机起始信号 ---- */
    dht11_set_output();
    HAL_GPIO_WritePin(DHT11_GPIO_Port, DHT11_Pin, GPIO_PIN_RESET); /* 拉低 */
    HAL_Delay(18);
    
    taskENTER_CRITICAL(); /* >=18ms, 粗延时用 HAL */
    HAL_GPIO_WritePin(DHT11_GPIO_Port, DHT11_Pin, GPIO_PIN_SET);   /* 松手 */
    delay_us(30);                                                  /* 拉高 20~40us 等 DHT11 反应 */
    dht11_set_input();                                             /* 切输入: 既松手又准备读 */

    /* ---- (2) 等 DHT11 响应: 低 80us -> 高 80us ---- */
    if (dht11_wait_level(GPIO_PIN_RESET, 100)) { ret = 1; goto out; };  /* 等变低(响应来了) */
    if (dht11_wait_level(GPIO_PIN_SET,  100)) { ret = 1; goto out; };   /* 等变高(80us 低结束) */
    if (dht11_wait_level(GPIO_PIN_RESET, 100)) { ret = 1; goto out; };  /* 等再变低(80us 高结束, 数据要来) */

    /* ---- (3) 读 40 位 = 5 字节, 高位先出 ---- */
    for (i = 0; i < 5; i++)
    {
        byte = 0;
        for (j = 0; j < 8; j++)
        {
            /* 每位以 50us 低开始: 先等低结束, 高电平开始 */
            if (dht11_wait_level(GPIO_PIN_SET, 100)) { ret = 1; goto out; };

            /* 高电平开始后延时 40us 再读一眼: 0(高~28us)已掉回低, 1(高70us)仍是高 */
            delay_us(40);

            byte <<= 1;   /* 高位先出: 先左移腾出最低位 */
            if (HAL_GPIO_ReadPin(DHT11_GPIO_Port, DHT11_Pin) == GPIO_PIN_SET)
            {
                byte |= 1;   /* 这一眼是高 -> 该位为 1 */
            }

            /* 若该位是 1, 此刻仍在高电平里, 等它掉回低, 对齐下一位 */
            if (dht11_wait_level(GPIO_PIN_RESET, 100)) { ret = 1; goto out; };
        }
        data[i] = byte;
    }

out:
    taskEXIT_CRITICAL();
    /* ---- (4) 校验: 前 4 字节之和的末 8 位 == 第 5 字节 ---- */
    if (ret == 0 &&
        (uint8_t)(data[0] + data[1] + data[2] + data[3]) != data[4]) {
        ret = 2;                              // 校验放临界区外
    }
    return ret;
}