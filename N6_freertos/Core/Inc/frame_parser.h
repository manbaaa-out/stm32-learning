#ifndef FRAME_PARSER_H
#define FRAME_PARSER_H

#include <stdint.h>

/* M5 协议解析 FSM,纯 C 版(STM32 端)。
 * 与网关端 src/protocol/FrameParser.{h,cpp} 同协议、同状态机,
 * 区别:无类/无 vector/无 std::function,改 struct + 定长数组 + 函数指针。 */

#define FP_LEN_MAX   64    /* 协议 §3.2:LEN 上限 */
#define FP_LEN_MIN   1
#define FP_HEADER1   0xAA
#define FP_HEADER2   0x55
/* payload 最大字节数 = LEN_MAX - 1(LEN 含 TYPE 1 字节) */
#define FP_PAYLOAD_MAX (FP_LEN_MAX - 1)

/* 8 状态,与 C++ 版 enum class State 一一对应 */
typedef enum {
    FP_WAIT_HEADER1 = 0,
    FP_WAIT_HEADER2,
    FP_WAIT_LEN,
    FP_WAIT_TYPE,
    FP_READ_PAYLOAD,
    FP_WAIT_CRC_LO,
    FP_WAIT_CRC_HI,
    FP_DELIVER
} fp_state_t;

/* 收齐一帧且 CRC 正确时的回调(替代 C++ std::function)。
 * 不带 user_data:解析层只交付 type/payload/len,不掺业务。 */
typedef void (*fp_frame_cb_t)(uint8_t type, const uint8_t *payload, uint8_t len);

/* FSM 全部状态打包进 struct(替代 C++ 类的成员变量) */
typedef struct {
    fp_state_t state;                       /* 当前状态 */
    uint8_t    len;                         /* 本帧 LEN */
    uint8_t    type;                        /* 本帧 TYPE */
    uint8_t    payload[FP_PAYLOAD_MAX];     /* 定长数组替代 std::vector */
    uint8_t    bytes_received;              /* payload 已收字节数 */
    uint16_t   crc;                         /* CRC 累加值(替代 CRC16 子对象) */
    uint8_t    crc_lo;
    uint8_t    crc_hi;
    fp_frame_cb_t on_frame;                 /* 帧回调(函数指针) */
} FrameParser;

/* 初始化:置初态、装回调(替代 C++ 构造 + setOnFrame) */
void frame_parser_init(FrameParser *fp, fp_frame_cb_t cb);

/* 喂一个字节,驱动一次状态转移(替代 C++ feed) */
void frame_parser_feed(FrameParser *fp, uint8_t byte);

#endif /* FRAME_PARSER_H */