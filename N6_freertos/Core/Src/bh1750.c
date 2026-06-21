#include "bh1750.h"
#include "dwt_delay.h"   /* delay_us:总线恢复时手动翻转 SCL 用 */
#include <stdint.h>
#include <stdio.h>
#include "FreeRTOS.h"   /* 必须在 task.h 之前: 它带入 FreeRTOSConfig.h + portmacro.h */
#include "task.h"

/* ===== I2C 总线/外设恢复 =====
   STM32F1 硬件 I2C 容易进异常态(BUSY 卡死、从机半路拽住 SDA)。出错后:
   1) DeInit 释放外设;2) 把 PB6/PB7 临时当 GPIO,松开 SDA、手动拍 9 个 SCL,
   把可能停在 ACK/数据位的从机移位寄存器走完;3) 补一个 STOP;4) 重建外设。
   这是单总线/I2C 解锁的标准手法,能把"时好时坏"里那类外设级卡死解开。 */
static void bh1750_bus_recover(void)
{
    HAL_I2C_DeInit(&hi2c1);                 /* MspDeInit 会把 PB6/PB7 退出 AF */

    __HAL_RCC_GPIOB_CLK_ENABLE();
    GPIO_InitTypeDef g = {0};
    g.Pin   = GPIO_PIN_6 | GPIO_PIN_7;      /* PB6=SCL, PB7=SDA */
    g.Mode  = GPIO_MODE_OUTPUT_OD;          /* 开漏,松手靠外部上拉回高 */
    g.Pull  = GPIO_NOPULL;
    g.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOB, &g);

    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_7, GPIO_PIN_SET);   /* 先松开 SDA */
    for (int i = 0; i < 9; i++) {                          /* 9 个时钟把从机拍出来 */
        HAL_GPIO_WritePin(GPIOB, GPIO_PIN_6, GPIO_PIN_RESET); delay_us(5);
        HAL_GPIO_WritePin(GPIOB, GPIO_PIN_6, GPIO_PIN_SET);   delay_us(5);
    }
    /* 补 STOP:SCL 高电平期间 SDA 由低拉高 */
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_7, GPIO_PIN_RESET); delay_us(5);
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_6, GPIO_PIN_SET);   delay_us(5);
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_7, GPIO_PIN_SET);   delay_us(5);

    MX_I2C1_Init();                          /* 重建外设(MspInit 把 PB6/7 复原成 AF-OD) */
}

/* 上电 + 设连续高分辨率模式。开机调一次(原来漏调了),让芯片处于确定状态。 */
void BH1750_Init(void)
{
    uint8_t cmd;
    cmd = BH1750_POWER_ON;      HAL_I2C_Master_Transmit(&hi2c1, BH1750_ADDR, &cmd, 1, 100);
    cmd = BH1750_CONT_HIGH_RES; HAL_I2C_Master_Transmit(&hi2c1, BH1750_ADDR, &cmd, 1, 100);
}

/* 单次测量:发模式 -> 等转换 -> 读 2 字节。任一步 I2C 不 OK 返回错误码。 */
static HAL_StatusTypeDef bh1750_read_once(uint16_t *raw)
{
    uint8_t cmd = BH1750_CONT_HIGH_RES;
    uint8_t buf[2] = {0};                   // 初始化,避免失败时读到垃圾
    HAL_StatusTypeDef st;

    st = HAL_I2C_Master_Transmit(&hi2c1, BH1750_ADDR, &cmd, 1, 100);
    if (st != HAL_OK) return st;

    vTaskDelay(180);                        // 高分辨率转换最长 ~180ms

    st = HAL_I2C_Master_Receive(&hi2c1, BH1750_ADDR, buf, 2, 100);
    if (st != HAL_OK) return st;

    *raw = (uint16_t)((buf[0] << 8) | buf[1]);
    return HAL_OK;
}

float BH1750_ReadLux(void)
{
    uint16_t raw;

    /* 先正常读一次;失败就恢复总线 + 重新上电,再试一次(自愈一次瞬时毛刺,
       这样偶发 I2C 错误不会直接把这一轮判成"故障",消掉时好时坏的抖动)。 */
    if (bh1750_read_once(&raw) != HAL_OK) {
        bh1750_bus_recover();
        BH1750_Init();
        if (bh1750_read_once(&raw) != HAL_OK) {
            return -1.0f;                   // 恢复后仍失败,才真判故障(多半是物理接线/上拉问题)
        }
    }
    return raw / 1.2f;
}
