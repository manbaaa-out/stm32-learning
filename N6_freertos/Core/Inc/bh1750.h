#ifndef __BH1750_H__
#define __BH1750_H__

#include "i2c.h"   // 需要用到 hi2c1 句柄和 HAL I2C 函数

/* ---- BH1750 I2C 地址 ---- */
// 7位地址 0x23(ADDR脚接地/悬空),左移1位 = 0x46,供 HAL 使用
#define BH1750_ADDR   (0x23 << 1)

/* ---- BH1750 指令码 ---- */
#define BH1750_POWER_ON      0x01   // 通电
#define BH1750_RESET         0x07   // 复位数据寄存器
#define BH1750_CONT_HIGH_RES 0x10   // 连续高分辨率模式(1 lx, 约120ms)

void  BH1750_Init(void);
float BH1750_ReadLux(void);

#endif /* __BH1750_H__ */