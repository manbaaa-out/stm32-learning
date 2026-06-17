#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H

/* ====== 区块在下面逐块填 ====== */

#define configCPU_CLOCK_HZ          ( 72000000UL )   /* CPU 主频, 你 N1 配的 72MHz */
#define configTICK_RATE_HZ          ( 1000 )         /* 节拍频率 1000Hz, 即每 1ms 一个 tick */
#define configUSE_PREEMPTION        1                /* 1=抢占式调度 */
#define configMAX_PRIORITIES        ( 7 )            /* 任务优先级档数 0~6 */
#define configMINIMAL_STACK_SIZE    ( 128 )          /* 空闲任务的栈大小, 单位是"字"(4字节), 即 512 字节 */
#define configMAX_TASK_NAME_LEN     ( 16 )           /* 任务名最大字符数 */
#define configUSE_16_BIT_TICKS      0                /* 0=tick 计数用 32 位 */

#define configSUPPORT_DYNAMIC_ALLOCATION    1               /* 支持动态创建任务(xTaskCreate) */
#define configSUPPORT_STATIC_ALLOCATION     0               /* 暂不用静态创建 */
#define configTOTAL_HEAP_SIZE       ( ( size_t ) ( 10 * 1024 ) )  /* FreeRTOS 堆总大小 10KB */

/* Cortex-M 中断优先级配置 */
#ifdef __NVIC_PRIO_BITS
    #define configPRIO_BITS         __NVIC_PRIO_BITS    /* 用 CMSIS 头里定义的, STM32=4 */
#else
    #define configPRIO_BITS         4
#endif

/* 最低优先级(数字最大): 内核 PendSV/SysTick 用它 */
#define configLIBRARY_LOWEST_INTERRUPT_PRIORITY         15

/* "那条线": 调用 FreeRTOS API 的中断, 优先级数字必须 >= 这个值 */
#define configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY    5

/* 下面两个是上面两个左移到 NVIC 寄存器实际位置, 不要手改 */
#define configKERNEL_INTERRUPT_PRIORITY \
    ( configLIBRARY_LOWEST_INTERRUPT_PRIORITY << (8 - configPRIO_BITS) )
#define configMAX_SYSCALL_INTERRUPT_PRIORITY \
    ( configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY << (8 - configPRIO_BITS) )

/* 把 port.c 里的函数名映射到向量表期望的名字 */
#define vPortSVCHandler         SVC_Handler
#define xPortPendSVHandler      PendSV_Handler
#define xPortSysTickHandler     SysTick_Handler

#define configUSE_IDLE_HOOK         0
#define configUSE_TICK_HOOK         0
#define configUSE_MUTEXES           1   /* 互斥量, 后面同步要用 */
#define configUSE_TIMERS            1   /* 软件定时器, timers.c 需要它 */
#define configTIMER_TASK_PRIORITY   ( 2 )
#define configTIMER_QUEUE_LENGTH    10
#define configTIMER_TASK_STACK_DEPTH ( configMINIMAL_STACK_SIZE * 2 )
#define configUSE_COUNTING_SEMAPHORES 1
#define configCHECK_FOR_STACK_OVERFLOW 0  /* 先关, 跑通后再开成 2 调试栈溢出 */
#define configUSE_16_BIT_TICKS      0

/* 断言失败时停在这里, 调试器一看就知道哪行触发 */
#define configASSERT( x )  if( ( x ) == 0 ) { taskDISABLE_INTERRUPTS(); for( ;; ); }

/* ====== 区块七: API 裁剪 (INCLUDE_ 决定哪些函数编进来) ====== */
#define INCLUDE_vTaskPrioritySet            1
#define INCLUDE_uxTaskPriorityGet           1
#define INCLUDE_vTaskDelete                 1   /* 任务有管理地退出(第四讲) */
#define INCLUDE_vTaskSuspend                1
#define INCLUDE_vTaskDelayUntil             1   /* 精确周期延时, 后面会用 */
#define INCLUDE_vTaskDelay                  1   /* ← 就是它! 解决当前报错 */
#define INCLUDE_xTaskGetSchedulerState      1
#define INCLUDE_uxTaskGetStackHighWaterMark 1   /* 查栈用量(我前面提的实测手段) */

#endif /* FREERTOS_CONFIG_H */