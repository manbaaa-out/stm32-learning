#include "bh1750.h"
#include <stdint.h>
#include <stdio.h>
#include "FreeRTOS.h"   /* 必须在 task.h 之前: 它带入 FreeRTOSConfig.h + portmacro.h */
#include "task.h"

void BH1750_Init() {
    uint8_t cmd = BH1750_POWER_ON;
    HAL_I2C_Master_Transmit(&hi2c1, BH1750_ADDR, &cmd, 1, 100);
}

float BH1750_ReadLux(void)
{
    uint8_t cmd = BH1750_CONT_HIGH_RES;
    uint8_t buf[2] = {0};              // 初始化,避免失败时读到垃圾值
    uint16_t raw;
    HAL_StatusTypeDef st;             // 接收返回值

    st = HAL_I2C_Master_Transmit(&hi2c1, BH1750_ADDR, &cmd, 1, 100);
    if (st != HAL_OK) {
        return -1.0f;
    }

    vTaskDelay(180);

    st = HAL_I2C_Master_Receive(&hi2c1, BH1750_ADDR, buf, 2, 100);
    if (st != HAL_OK) {
        return -1.0f;
    }

    raw = (buf[0] << 8) | buf[1];
    return raw / 1.2f;
}