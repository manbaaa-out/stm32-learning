# N6 FreeRTOS 移植原理 面试 Flashcard

> 主题:把 FreeRTOS 移植到 STM32F103 的本质 —— 上下文切换、节拍、堆栈、中断优先级。
> 这是 N6 最硬、最能体现"摸透原理"的一组,面试官会逐层深挖。

---

## 第一部分:纯面试(通用知识,不绑项目)

### A1. 什么是上下文切换?Cortex-M3 上切换一个任务要保存/恢复哪些寄存器?

**答**:上下文切换是指 CPU 从一个执行流切到另一个时,保存旧执行流的"现场"、恢复新执行流的"现场"。现场就是 CPU 寄存器集合。Cortex-M3 上一共 16 个核心寄存器要处理:R0–R12、SP(R13)、LR(R14)、PC(R15)、xPSR。

**怎么答出深度**:点出 Cortex-M3 的硬件分工 —— 进异常时硬件**自动入栈** 8 个(R0–R3、R12、LR、PC、xPSR,称 stack frame),剩下的 R4–R11 这 8 个由**软件手动入栈**。所以 PendSV 汇编里核心动作就是存/取 R4–R11 + 切换栈指针,另一半硬件包办。能讲清"硬件存一半、软件存一半"就显出真懂硬件,而不是背概念。

**措辞陷阱**:别说"切换时要保存 TCB"。TCB(Task Control Block,任务控制块)是任务的**身份档案**(常驻内存,记优先级、状态、栈顶指针字段),它是切换时的**索引**,不是被搬运的现场。被保存/恢复的是寄存器,TCB 里只更新"栈顶指针"这一个字段。

---

### A2. PendSV 异常在 RTOS 里起什么作用?为什么用它做上下文切换?

**答**:PendSV(Pendable Service Call,可挂起的系统调用)是 Cortex-M 专门为 OS 设计的一个异常。RTOS 把实际的任务切换动作放在 PendSV 的处理函数里。需要切换时(节拍到了、或任务主动让出),内核把 PendSV 异常"挂起"(置 pending 位),等所有更高优先级中断处理完后,PendSV 才执行,完成切换。

**怎么答出深度**:关键是 PendSV 被设成**最低优先级**。这样任务切换永远不会插在别的中断前面 —— 任何真正紧急的硬件中断都能先于"切换任务"被响应。讲出"内核把自己放最低,给所有紧急中断让路"这一层设计哲学,是加分点。

**措辞陷阱**:不要说"PendSV 优先级最高所以负责切换"。恰恰相反,**它优先级最低**。原因正是实时性 —— 切换任务这件事本身不紧急,不能挡住急活。

---

### A3. RTOS 的"节拍(tick)"是什么?在 Cortex-M 上用什么产生?

**答**:节拍是内核的时间基准(心跳)。内核靠它计时:谁的延时到期该唤醒、时间片到没到。在 Cortex-M 上由 **SysTick**(System Tick Timer,系统滴答定时器)周期性产生中断,每次中断内核 tick 计数加一。

**怎么答出深度**:能写出节拍频率和重装载值的关系 —— SysTick 重装载值 = 内核时钟 / 节拍频率 - 1。比如 72MHz 主频、1000Hz 节拍,重装载值 = 72000000/1000 - 1 = 71999,即每 1ms 一个节拍。这个换算关系能讲清,说明你看过 port 层而不只是用 API。

**措辞陷阱**:SysTick 中断里**不直接做切换**。它只做两件:tick 计数加一、判断是否需要切换;若需要,只是**挂起 PendSV**,真正的切换在 PendSV 里完成。把"判断"和"执行切换"分到两个异常,是 Cortex-M RTOS 的标准结构。

---

### A4. 任务的栈从哪里来?和"堆"是什么关系?

**答**:每个任务有自己独立的栈,用来存它的现场和局部变量。动态创建任务时(如 xTaskCreate),内核从 FreeRTOS 管理的**堆**里分一块内存当这个任务的栈,栈顶地址记进该任务的 TCB。

**怎么答出深度**:说清"堆"是 FreeRTOS 自己管的一块内存(由 heap_x.c 实现,本质是个大数组),不是 C 标准库的 malloc 堆。并能讲 heap_4 的特点:能分配、能释放、能合并相邻空闲块(防碎片化),是最常用的选择。

**措辞陷阱**:别把任务栈和系统主栈混为一谈。Cortex-M 有两个栈指针:MSP(Main Stack Pointer)给内核/中断用,PSP(Process Stack Pointer)给任务用。任务切换本质就是改 PSP 指向新任务的栈。

---

### A5. 为什么"调用 FreeRTOS API 的中断"优先级数字不能太小?

**答**:FreeRTOS 进临界区是靠 Cortex-M 的 BASEPRI 寄存器**屏蔽一部分中断**(屏蔽优先级数字 ≥ 阈值的),而不是粗暴关掉所有中断。这个阈值就是 configMAX_SYSCALL_INTERRUPT_PRIORITY。优先级数字小于阈值的中断不受这个屏蔽控制,能打断正在临界区里改内核数据结构的内核 —— 若这种中断里又调 FreeRTOS API 去改同一个数据结构,就会损坏内核数据。

**怎么答出深度**:讲清这把中断劈成两类:数字 ≥ 阈值的"受管中断"(临界区能屏蔽它,允许调 ...FromISR API);数字 < 阈值的"超管中断"(临界区屏蔽不了,享受硬实时,但禁止调任何 FreeRTOS API)。能说出"实时性与可调 API 之间的取舍由这条线划分"是深度体现。

**措辞陷阱**:注意 Cortex-M 优先级**数字越小优先级越高**(0 最高)。所以"优先级太高不能调 API"指的是"数字太小";"优先级 ≥ 阈值"指的是"数字 ≥ 阈值"。这个反直觉点最容易说反。

---

### A6. "移植 FreeRTOS"的本质是什么?用两三句话概括。

**答**:移植不是抄文件,本质是把内核与硬件相关的三件事在具体 MCU 上接通:① 上下文切换 —— 靠 PendSV 异常实现寄存器现场的存取;② 节拍 —— 靠 SysTick 给内核当心跳;③ 堆栈 —— 给每个任务从堆里分独立的栈。这三件事都落在 port 层(port.c + portmacro.h)和 FreeRTOSConfig.h 里,所谓移植就是把它们和目标 MCU 的时钟、中断、内存对齐。

**怎么答出深度**:能区分"内核无关代码"(tasks.c/queue.c/list.c,所有平台通用)和"port 层"(和 CPU 架构、工具链绑定)。强调"移植主要是配置 port 层和 FreeRTOSConfig.h,内核源码不动",显出对 FreeRTOS 分层架构的理解。

---

## 第二部分:项目挂钩(我 N6 实际怎么做的)

### P1. 你的 FreeRTOS 移植工具链是什么?踩过什么坑?

我用的是 **arm-none-eabi-gcc(GCC/newlib)+ CMake/Ninja**,不是网上教程主流的 Keil。这导致一个必须警惕的坑:port 文件必须取 `portable/GCC/ARM_CM3/port.c`,**不是** RVDS 目录(那是 Keil/ARMCC 的,汇编语法不同)。网上 Keil 教程说"留 RVDS、删 GCC",对我正好相反。这和我之前 printf 重定向那次"照搬错工具链文档"是同类陷阱,所以底层文件我都先确认工具链再动手。

### P2. 移植时撞到的第一个链接错误是什么?怎么解决?

`multiple definition of PendSV_Handler / SysTick_Handler / SVC_Handler`。根源:CubeMX 生成的 stm32f1xx_it.c 里有这三个空 Handler,而 port.c 里也定义了它们(名字是 `xPortPendSVHandler` 等),两份外部链接的同名全局符号冲突。解决用两步:① 在 FreeRTOSConfig.h 写三个重命名宏(`#define xPortPendSVHandler PendSV_Handler` 等),把 port.c 的函数名映射到向量表期望的名字;② 让 CubeMX 不生成这三个(NVIC 的 Code generation 标签里取消勾选 SVC/PendSV/SysTick 的 IRQ handler),否则每次重新生成代码它们会复活。我中途就因为重新生成配 USART 时这三个复活,撞过一次同样的链接错。

### P3. SysTick 双主冲突怎么处理?

CubeMX/HAL 默认用 SysTick 给 HAL_Delay 当时基,而 FreeRTOS 也要 SysTick 当节拍器,两个主人抢一个外设。而且要求相反:HAL 要 SysTick 高优先级,FreeRTOS 要它最低。解决:在 CubeMX 的 SYS → Timebase Source 把 HAL 时基从 SysTick 改成 **TIM4**,SysTick 交给 FreeRTOS。改完会多生成 stm32f1xx_hal_timebase_tim.c,HAL_GetTick 改由 TIM4 中断喂(我 N2 防抖、N5 超时都依赖 HAL_GetTick,所以这步必须确认 TIM4 时基正常)。

### P4. 你的 FreeRTOSConfig.h 关键配置值?

configCPU_CLOCK_HZ = 72000000(N1 配的主频)、configTICK_RATE_HZ = 1000(1ms 节拍)、configTOTAL_HEAP_SIZE = 10KB、heap_4、configMAX_SYSCALL_INTERRUPT_PRIORITY 对应的"线"= 5(库优先级值)、configPRIO_BITS = 4(STM32 的 NVIC 实现 4 位优先级)。优先级分组用 NVIC_PRIORITYGROUP_4(抢占 4 位、子优先级 0 位),因为 FreeRTOS 要求全部位都作抢占优先级。

### P5. 移植成功的验证标准是什么?

不是"编译通过",是**烧进硬件、任务真的在调度下运行**。我用 PC13 LED 闪烁验证:LED 准时 500ms 一闪,说明调度器启动成功、PendSV 切换正常、SysTick 节拍在跳、三个 Handler 重命名生效、vTaskDelay 的睡眠-唤醒机制工作。编译通过只证明符号对了,LED 闪才证明整条链路在硬件上兑现。

### P6. RAM 占用从 1832B 跳到 12KB,为什么?

加 xTaskCreate 前,链接器把没人用的 10KB 堆数组(heap_4 里的 ucHeap)优化掉了;加任务后真有人用它,优化不掉,这 10KB .bss 实打实算进 RAM。这印证了一个 C 内存布局点:初值为零的大数组在 .bss,编译期的 FLASH 报告里看不到,但运行时真占 RAM。F103C8T6 只有 20KB RAM,10KB 堆占一半,所以任务和栈的预算要精打细算。

---

*作者:Bi(2026 秋招准备,项目二 N6 FreeRTOS)*
*配套实测:CubeMX(TIM4 时基)→ GCC/ARM_CM3 port → heap_4 → FreeRTOSConfig 七区块 → 单任务 LED 硬件验证通过(PC13 准时 500ms 闪);移植全链路坑(Handler 重定义、SysTick 双主、工具链 port 目录)均亲历并解决*
