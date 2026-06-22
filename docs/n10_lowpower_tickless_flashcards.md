# N10 速记卡：低功耗 Sleep + FreeRTOS Tickless Idle

> 项目二 STM32+FreeRTOS 传感器节点 / N10
> 两段式：**Part 1** 纯面试通用考点（脱离本项目也成立）；**Part 2** 本项目落地细节（结合 N6 代码与实测）。
> 硬件：Blue Pill STM32F103C8T6 @72MHz，HSE 8MHz×9。FreeRTOS 原生 ARM_CM3 端口（手动集成）。

---

## 卡 1 — 三种低功耗模式的区别与选型

**Part 1（通用）**
STM32 三档低功耗，按"停的东西越来越多"排序：
- **Sleep**：只停 CPU 内核时钟；外设、所有 RAM/寄存器照常。任何中断立刻唤醒，醒来从停下处继续。省电有限，唤醒近零代价。类比"合盖子"。
- **Stop**：停 CPU + 大部分时钟，**连主时钟（HSE/PLL）都关**；RAM/寄存器靠供电保住。省电大幅提升（mA→µA）。代价：主时钟停了，唤醒要重新拉时钟，且能唤醒的源大大减少（普通定时器、UART 都不走了）。类比"休眠到硬盘"。
- **Standby**：最深，几乎全断电，RAM 基本丢失（仅极少后备区保留）。最省电。醒来等于复位重启，程序从 main 重跑。类比"关机"。

**Part 2（本项目）**
选 **Sleep**。决定性约束：网关命令要求**秒级响应**，而命令走 UART——Stop 把主时钟停了，UART 不工作，字节直接丢、收不到命令，做不到秒级。Sleep 是"秒级响应"硬约束下能取的最深睡眠。这不是妥协，是物理唯一解。

---

## 卡 2 — "睡得越深，能叫醒的门铃越少"这一权衡

**Part 1（通用）**
低功耗的根本权衡：**睡眠深度 ↔ 唤醒源数量/响应能力**，没有免费午餐。
- 想省电极致 → 睡深 → 外设（含 UART）停 → 收不到即时事件
- 想随时响应 → 外设得活 → 睡不深 → 省电有限
真实产品两条路线：(A) 浅睡 Sleep，随时响应，省电有限；(B) 深睡 Stop/Standby，定时自醒集中处理，命令延迟到分钟级，适合几年一换电池的设备。

**Part 2（本项目）**
节点需求二元对立：① 每 5 分钟定时采样（可预测，设闹钟即可）；② 随时响应命令（不可预测，网关何时发未知）。
矛盾点：命令从 UART 进，UART 收字节需要时钟在走。选路线 A（Sleep）后两者兼得——**闹钟（SysTick 一次性定时）保证定时醒，中断（UART RX）保证随时醒**。

---

## 卡 3 — Tickless Idle 是什么 / 为什么要"停滴答"

**Part 1（通用）**
正常态 SysTick 每 1ms 中断一次给 RTOS 报时。若 CPU 睡着，这个 1ms 中断会**每 1ms 把核吵醒一次**，等于没睡——且每次进出睡眠都有开销，1 秒被吵醒 1000 次，省的电不够付唤醒成本。
Tickless（无滴答空闲）：进空闲时不再让 CPU 空转，而是 (1) 算出"离下一个该醒的任务还有多久"，(2) 把 SysTick 改成"睡到那一刻的一次性长闹钟"，(3) 睡（WFI），(4) 醒来把睡过去的时间**一次性补记**到 tick 计数。
**本质一句话**：不是不报时了，而是把"每 1ms 固定滴答"换成"按需设置、睡到下一事件点的一次性闹钟"——既睡得久，又不丢时间。

**Part 2（本项目）**
开关：`FreeRTOSConfig.h` 加 `#define configUSE_TICKLESS_IDLE 1`，激活 port.c 里 `#if (configUSE_TICKLESS_IDLE == 1)` 那段内置 `vPortSuppressTicksAndSleep`，**一行 C 不用自己写**。三个任务全阻塞（`vTaskDelayUntil` / `xStreamBufferReceive` / `xQueueReceive` 超时）时，调度器进空闲任务，触发该函数。

---

## 卡 4 — port.c 内置实现的三段核心

**Part 1（通用）**
ARM_CM3 端口 `vPortSuppressTicksAndSleep`（`configUSE_TICKLESS_IDLE==1`）三段：
1. **重设闹钟**：`ulReloadValue = 当前剩余 + ulTimerCountsForOneTick × (xExpectedIdleTime-1)` → 写 SysTick LOAD，把固定滴答改成一次性长定时。
2. **睡**：`configPRE_SLEEP_PROCESSING()` → `WFI` → `configPOST_SLEEP_PROCESSING()`。WFI 是 Cortex-M 停核等中断的指令。
3. **补时间**：靠 `portNVIC_SYSTICK_COUNT_FLAG_BIT` 判断是闹钟叫醒还是其他中断提前叫醒，算出实际睡了几拍，`vTaskStepTick(ulCompleteTickPeriods)` 补给内核。

**Part 2（本项目）**
- `eTaskConfirmSleepModeStatus()==eAbortSleep` 时放弃睡眠（有 context switch 待处理）——保证只在真正无事时才睡。
- 实测 `xTickCount` 14s 推进 ~13767 拍，证明 `vTaskStepTick` 补时间在工作，内核时钟没在睡眠里丢，`vTaskDelayUntil` 1s 栅格不漂。
- FLASH +1024B（≈1KB）即这段函数 + 两个钩子展开被链接进来；RAM +8B 是三个 `#if TICKLESS` 静态变量（ulTimerCountsForOneTick 等）。

---

## 卡 5 — configPRE/POST_SLEEP_PROCESSING 钩子的时机为何是唯一安全窗口

**Part 1（通用）**
PRE/POST 钩子默认空，留给应用填"睡前关、醒后开"的额外动作（典型：关闭会在睡眠期间持续吵醒 CPU 的定时器、关外设时钟）。
执行时机被 port.c 卡死：PRE 在 **WFI 前一瞬间**、POST 在**醒来后立即**。能走到 PRE 的前提是 `eTaskConfirmSleepModeStatus` 已确认"所有任务阻塞、没活干"——**此刻没有任何任务在跑**。这就是"睡前动作"的唯一安全窗口：此时关掉某些东西不会影响任何正在执行的代码。

**Part 2（本项目）**
钩子里挂 `HAL_SuspendTick()`（PRE）/ `HAL_ResumeTick()`（POST），停/恢复 TIM4 时基中断。
为何不能在任务里提前关 TIM4：TIM4 兼着 HAL 时间基准（每 1ms 涨 `uwTick`），`HAL_Delay`、HAL 各 timeout 函数（如 `HAL_I2C_Master_Transmit(...,100)`、`DHT11_Read` 里 `HAL_Delay(18)`）全靠 `uwTick`。提前关 → `uwTick` 冻住 → 下次采样调 HAL 计时函数时卡死/超时失效。只有在"确认无任务运行"之后关才安全。
> FreeRTOSConfig.h 不能 include `stm32f1xx_hal.h`（会污染），故用 `extern void HAL_SuspendTick(void);` 声明后再在宏里调用。

---

## 卡 6 — TIM4 在睡眠下必须停的双重理由

**Part 1（通用）**
HAL 默认时基是 SysTick；但工程跑 FreeRTOS 时，CubeMX 会把 SysTick 让给 FreeRTOS 当调度滴答，给 HAL 另开一个硬件定时器（本项目 TIM4）做时基，避免两者抢同一个 SysTick。结果：**FreeRTOS 用 SysTick，HAL 用 TIM4**，两个独立定时器。

**Part 2（本项目）**
PRE 钩子里关 TIM4 是**一箭双雕**：
1. **冻 uwTick**：避免无任务时 `uwTick` 无谓地涨（次要）。
2. **堵唤醒源**：Sleep 只停 CPU 内核时钟，外设时钟没停，TIM4 会每 1ms 中断一次把核从 WFI 反复叫醒——而 FreeRTOS tickless **只停 SysTick，根本不知道还有个 TIM4 在捅核**。不关 TIM4，tickless 开了也白开（电流不降，对应社区"开了 tickless 电流还是 5mA"的同类坑）。这是主要理由。

---

## 卡 7 — IWDG 的 LSI 独立时钟与睡眠交互（N9 伏笔兑现）

**Part 1（通用）**
IWDG（独立看门狗）时钟源是 LSI（内部低速 RC，本项目 ~40kHz），**独立于主时钟系统**。Sleep 停的是 CPU 内核时钟，**不停 LSI**——所以 IWDG 在 Sleep 期间倒计时照常进行，超时到了照样复位。好处：睡眠时看门狗仍在保护；隐患：若某次唤醒被延迟（如时钟恢复耗时），喂狗可能踩线。

**Part 2（本项目）**
喂狗节奏：`vSampleReportTask` 每 1s 喂一次（全员报到，三任务凑齐 `WDG_BIT_ALL` 才 `HAL_IWDG_Refresh`），IWDG 超时 ~2s（prescaler 64 + reload 1250）。
关键推理：**tickless 不改变任务唤醒时刻，只改变睡眠期间 CPU 停不停**。`vTaskDelayUntil` 的 1s 唤醒、`xStreamBufferReceive`/`xQueueReceive` 的 500ms 超时唤醒，都被 tickless 正确计入"下一个唤醒点"，照常醒、照常打卡喂狗。故 1s 喂 / 500ms 打卡 / 2s 超时这套节奏在 Sleep 下原样运转，**无需在钩子里额外喂狗**。
实测兑现：静置 14s（2s 超时的 7 倍），`RCC_CSR` 的 `IWDGRSTF` 始终为 0 → 一次都没误复位。

---

## 卡 8 — 调试时为何要冻结 IWDG / TIM4

**Part 1（通用）**
调试器 halt/单步会停住 CPU，但 IWDG 的 LSI 不会因核停而停 → 暂停几秒就被看门狗复位，根本没法调。解法：通过 DBGMCU 调试控制寄存器置位，让调试暂停时冻结相应计数器（IWDG、各定时器）。

**Part 2（本项目）**
`MX_IWDG_Init()` 后加 `__HAL_DBGMCU_FREEZE_IWDG()` + `__HAL_DBGMCU_FREEZE_TIM4()`（裸 `SET_BIT(DBGMCU->CR, …)`，FLASH 仅 +28B）。
> **平台敏感坑**：原计划先调 `__HAL_RCC_DBGMCU_CLK_ENABLE()`，但 **F1 上该宏不存在**，链接报 `undefined reference`。原因：F1 的 DBGMCU 属 Cortex-M3 核心调试域，常通、无 APB 时钟门，HAL 未给 F1 定义此宏（只 F4/F7/L4 那类把 DBGMCU 挂 APB 上的型号才有）。两个 FREEZE 宏不依赖时钟使能，删掉该行即可。
> 教训同 N5 的 printf hook（Keil `fputc` vs GCC `__io_putchar`）：**型号/平台敏感的 HAL 宏不能跨系列照搬，必须对实际目标验证。**

---

## 卡 9 — 验证方法：如何证明"真的睡了 / 能醒 / 不误杀"

**Part 1（通用）**
低功耗落地后，"编译过 ≠ 行为对"，必须实测三件事：
1. **真睡**：调试器多次随机 halt 看 PC 是否停在 WFI 附近。
2. **能醒且响应**：触发外部事件（如串口发命令）看是否立即响应。
3. **看门狗不误杀**：静置远超超时时长，查复位标志位是否被置位。
4. **电流**（终极指标）：万用表对比开/关低功耗两版静态电流。

**Part 2（本项目）实测结果**
- 验证一：8/8 次 halt 全停在 `0x080093f0`（WFI 下一条），核绝大部分时间在睡。
- 验证二：每条 0x21/0x20 命令拿到 0x05 应答，41–222ms，延迟大头是 `sample_all` 同步读传感器（BH1750 ~180ms），纯"唤醒+往返"仅几十 ms，唤醒开销感觉不出。
- 验证三：`IWDGRSTF` 14s 始终 0；`xTickCount` 推进 ~13767 拍（补时间正常）。
- 验证四：电流需万用表，预期 F103@72MHz 基线 20–30mA，开 Sleep tickless 后空闲明显下降；降幅受板上 DHT11/BH1750 上拉与 PC13 LED 影响，非教科书式 mA→µA 剧跌。

---

## 一分钟自检（盖住答案，能否脱口而出）

1. Sleep / Stop / Standby 各停什么？为何本项目必须选 Sleep？
2. Tickless 为何要停 SysTick？停了靠什么报时？
3. `configPRE_SLEEP_PROCESSING` 钩子在什么时刻执行？为何是关 TIM4 的唯一安全窗口？
4. TIM4 在睡眠下必须停的两个理由分别是什么？哪个是主要的？
5. IWDG 的 LSI 在 Sleep 下停不停？喂狗节奏为何不受 tickless 影响？
6. 为何 `__HAL_RCC_DBGMCU_CLK_ENABLE()` 在 F1 上链接失败？
