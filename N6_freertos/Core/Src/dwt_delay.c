#include "dwt_delay.h"

/* 内核主频 (MHz)。你的 Blue Pill 是 HSE 8MHz x9 = 72MHz。
   1us = 72 个 CPU 周期。换主频只改这一处。 */
#define CPU_FREQ_MHZ  72U

void DWT_Init(void)
{
    /* (1) 打开 DWT 单元总电源。不开它, 下面 CYCCNT 设了也不会动。 */
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;

    /* (2) 周期计数器清零, 从 0 开始数。 */
    DWT->CYCCNT = 0;

    /* (3) 打开 CYCCNT 计数开关。从此 CPU 每过一个周期它 +1。 */
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
}

void delay_us(uint32_t us)
{
    uint32_t start = DWT->CYCCNT;        /* 记下出发点 */
    uint32_t ticks = us * CPU_FREQ_MHZ;  /* 要等的周期数 = us x 72 */

    /* 无符号 32 位减法自动回绕: 即使 CYCCNT 中途溢出, (now - start) 依然正确。
       单次最大约 59 秒 (2^32/72), 对 DHT11 的几十微秒绰绰有余。 */
    while ((DWT->CYCCNT - start) < ticks)
    {
        ;  /* 空转死等, 走够 ticks 个周期就退出 */
    }
}