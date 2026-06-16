#include "bh1750.h"
#include <stdint.h>

void BH1750_Init() {
    uint8_t cmd = BH1750_POWER_ON;
    HAL_I2C_Master_Transmit(&hi2c1, BH1750_ADDR, &cmd, 1, 100);
}

float BH1750_ReadLux(void)
{
    uint8_t cmd = BH1750_CONT_HIGH_RES;   // 指令码 0x10
    uint8_t buf[2];                        // 接收缓冲区:高字节、低字节
    uint16_t raw;                          // 拼接后的16位原始值

    // 第1步:发指令码,触发测量(用 Master_Transmit,发1字节)
    HAL_I2C_Master_Transmit(&hi2c1, BH1750_ADDR, &cmd, 1,100);


    // 第2步:等测量完成,高分辨率模式约120ms,保险起见等180ms
    HAL_Delay(180);


    // 第3步:读回2字节(用 Master_Receive,读2字节到 buf)
    HAL_I2C_Master_Receive(&hi2c1, BH1750_ADDR, buf, sizeof(buf), 100);


    // 第4步:拼接成16位
    raw = (buf[0] << 8) | buf[1];

    // 第5步:换算成 lux 并返回。公式:lux = raw / 1.2
    return raw / 1.2f;
}