#include "frame_parser.h"
#include "crc16.h"

void frame_parser_init(FrameParser *fp, fp_frame_cb_t cb) {
    fp->state          = FP_WAIT_HEADER1;
    fp->len            = 0;
    fp->type           = 0;
    fp->bytes_received = 0;
    fp->crc            = 0xFFFF;
    fp->crc_lo         = 0;
    fp->crc_hi         = 0;
    fp->on_frame       = cb;
    /* payload 数组不必清零:bytes_received 控制有效范围,读不到脏数据 */
}

/* 收齐一帧后处理:校验 CRC,对则回调交付,无论对错都 resync。
 * 对应 C++ 版 handleDeliver()。 */
static void frame_parser_deliver(FrameParser *fp) {
    uint16_t received = (uint16_t)(((uint16_t)fp->crc_hi << 8) | fp->crc_lo);
    if (fp->crc == received) {
        if (fp->on_frame) {
            /* payload 长度 = LEN - 1(LEN 含 TYPE) */
            fp->on_frame(fp->type, fp->payload, (uint8_t)(fp->len - 1));
        }
    }
    /* CRC 不匹配:此处可选串口调试输出 / 错误计数,不能用 std::cerr。
       例如 error_count++ 或重定向 printf。先留空,逻辑上直接 resync。 */
    fp->state = FP_WAIT_HEADER1;   /* 无论成功失败都 resync */
}

void frame_parser_feed(FrameParser *fp, uint8_t byte) {
    switch (fp->state) {
    case FP_WAIT_HEADER1:
        if (byte == FP_HEADER1) {
            fp->state = FP_WAIT_HEADER2;
        }
        /* 非 0xAA:静默丢弃,留在本状态(resync 的常态——一直找帧头) */
        break;

    case FP_WAIT_HEADER2:
        if (byte == FP_HEADER2) {
            fp->crc   = 0xFFFF;            /* 帧开始,复位 CRC(= C++ crc_.reset()) */
            fp->state = FP_WAIT_LEN;
        } else if (byte == FP_HEADER1) {
            /* 又一个 0xAA:自环,可能是新帧头开始 */
        } else {
            fp->state = FP_WAIT_HEADER1;   /* 其它字节:resync */
        }
        break;

    case FP_WAIT_LEN:
        if (byte < FP_LEN_MIN || byte > FP_LEN_MAX) {
            /* LEN 越界:Fail Fast,resync(此处可错误计数/调试输出,不用 std::cerr) */
            fp->state = FP_WAIT_HEADER1;
        } else {
            fp->len   = byte;
            fp->crc   = crc16_update(fp->crc, byte);
            fp->state = FP_WAIT_TYPE;
        }
        break;

    case FP_WAIT_TYPE:
        fp->type           = byte;
        fp->crc            = crc16_update(fp->crc, byte);
        fp->bytes_received = 0;
        if (fp->len == FP_LEN_MIN) {       /* LEN=1:无 payload,直接等 CRC */
            fp->state = FP_WAIT_CRC_LO;
        } else {
            fp->state = FP_READ_PAYLOAD;
        }
        break;

    case FP_READ_PAYLOAD:
        /* 定长数组,bytes_received 控制写入位置;有上界,无需 reserve/动态分配。
           理论上 len<=64 时 bytes_received 不会越界,但加一道保险更稳妥。 */
        if (fp->bytes_received < FP_PAYLOAD_MAX) {
            fp->payload[fp->bytes_received] = byte;
        }
        fp->bytes_received++;
        fp->crc = crc16_update(fp->crc, byte);
        if (fp->bytes_received >= (fp->len - 1)) {
            fp->state = FP_WAIT_CRC_LO;
        }
        break;

    case FP_WAIT_CRC_LO:
        fp->crc_lo = byte;
        fp->state  = FP_WAIT_CRC_HI;
        break;

    case FP_WAIT_CRC_HI:
        fp->crc_hi = byte;
        fp->state  = FP_DELIVER;
        frame_parser_deliver(fp);          /* 交付 + resync */
        break;

    case FP_DELIVER:
        /* 瞬态,正常不应停留;防御性 resync(原 C++ 版这里 std::cerr,MCU 改掉) */
        fp->state = FP_WAIT_HEADER1;
        break;

    default:
        fp->state = FP_WAIT_HEADER1;       /* 兜底 resync */
        break;
    }
}