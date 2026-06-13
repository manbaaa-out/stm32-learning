#ifndef CRC16_H
#define CRC16_H

#include <stdint.h>   // C 里整数定宽类型来自 <stdint.h>(C++ 才是 <cstdint>)

/* CRC16-MODBUS (多项式 0xA001, 初始值 0xFFFF)
 * 与网关端 src/protocol/CRC16.cpp 同源,算法逐字节一致,不可改动。
 * 设计为纯函数:状态由调用方持有,不引入对象。 */

/* 逐字节累加:传入当前 crc 和一个字节,返回更新后的 crc。
 * 给接收端 FSM 用——一个字节一个字节喂。
 * 用法:uint16_t crc = 0xFFFF; crc = crc16_update(crc, b); ... */
uint16_t crc16_update(uint16_t crc, uint8_t byte);

/* 一次性算整段 buffer 的 CRC(内部 crc=0xFFFF 后循环 crc16_update)。
 * 给发送端用——算整帧 CRC 一把出结果。 */
uint16_t crc16(const uint8_t *data, uint16_t len);

#endif /* CRC16_H */