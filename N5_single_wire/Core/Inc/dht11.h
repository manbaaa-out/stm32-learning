#ifndef __DHT11_H
#define __DHT11_H

#include "main.h"

/* 读取一次 DHT11。
   参数 data: 指向长度 >=5 的数组, 存放原始 5 字节
              data[0]=湿度整数 data[1]=湿度小数 data[2]=温度整数
              data[3]=温度小数 data[4]=校验和
   返回值: 0=成功  1=无响应(检查接线/供电)  2=校验错(数据传输出错, 可重读) */
uint8_t DHT11_Read(uint8_t *data);

#endif /* __DHT11_H */