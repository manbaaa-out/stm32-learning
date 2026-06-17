# ============================================================
# FreeRTOS Kernel 接入 (手动移植, GCC/ARM_CM3, heap_4)
# 内核位于工程根的上一级: STM32PROJECT/FreeRTOS-Kernel-main
# 从 N6_freertos/ 视角用 ../ 引用
# ============================================================

set(FREERTOS_DIR ${CMAKE_SOURCE_DIR}/../FreeRTOS-Kernel-main)

# 启动前先确认路径真的存在, 路径错了立刻在 configure 阶段报错,
# 而不是拖到编译期才爆一堆找不到头文件 (治"潜伏坑")
if(NOT EXISTS ${FREERTOS_DIR}/tasks.c)
    message(FATAL_ERROR "FreeRTOS not found at ${FREERTOS_DIR} -- 检查内核目录名/位置")
endif()

# 内核核心源码 + GCC port + heap_4
target_sources(${CMAKE_PROJECT_NAME} PRIVATE
    ${FREERTOS_DIR}/tasks.c
    ${FREERTOS_DIR}/list.c
    ${FREERTOS_DIR}/queue.c
    ${FREERTOS_DIR}/timers.c
    ${FREERTOS_DIR}/event_groups.c
    ${FREERTOS_DIR}/portable/GCC/ARM_CM3/port.c
    ${FREERTOS_DIR}/portable/MemMang/heap_4.c
)

# 头文件路径: API 头 + portmacro.h + FreeRTOSConfig.h(放在 Core/Inc)
target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
    ${FREERTOS_DIR}/include
    ${FREERTOS_DIR}/portable/GCC/ARM_CM3
    ${CMAKE_SOURCE_DIR}/Core/Inc
)