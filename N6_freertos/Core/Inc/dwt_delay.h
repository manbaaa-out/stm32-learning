#ifndef __DWT_DELAY_H
#define __DWT_DELAY_H

#include "main.h"   /* 间接包含 stm32f1xx.h, 提供 CoreDebug / DWT 定义及 Msk 宏 */

/* 初始化 DWT 周期计数器 (微秒延时的硬件基础), 在 main 里 MX_xxx_Init 之后调一次 */
void DWT_Init(void);

/* 忙等待延时 us 微秒 (基于 72MHz 内核, 1us = 72 周期) */
void delay_us(uint32_t us);

#endif /* __DWT_DELAY_H */