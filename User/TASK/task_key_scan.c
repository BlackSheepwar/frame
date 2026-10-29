/**
 * @file task_key_scan.c
 * @brief 任务层：按键扫描线程体
 * @author 李嘉图
 * @date 2026-10-22
 *
 * @note 这一层只做"节拍 + 调一步"，不含任何业务判断：
 *       周期在 .ioc 的任务配置和下面的宏里，业务在 APP 层。
 *       这样调周期只改一个数，改业务不碰调度。
 */

/*==============================================================================
 * 头文件包含
 *============================================================================*/
// 固定包含
#include <stddef.h>
#include "FreeRTOS.h"
#include "cmsis_os2.h"
// 功能包含
#include "svc_key_logic.h"

/*==============================================================================
 * 宏定义与常量
 *============================================================================*/
/** @brief 按键扫描周期（ms），与 20 ms 消抖窗口对应 */
#define TASK_KEY_SCAN_PERIOD_MS     10u

/*==============================================================================
 * 接口实现
 *============================================================================*/
/**
 * @brief 按键扫描线程体
 * @param argument 未使用
 * @note 用 osDelayUntil 按绝对节拍唤醒：每一拍都落在 10 ms 的整数倍上，
 *       所以传给业务层的间隔恒等于 TASK_KEY_SCAN_PERIOD_MS，
 *       不需要再取 tick 差值算 elapsed。
 *       代价是线程若被长时间抢占、错过了整拍，osDelayUntil 会立即返回，
 *       这一拍按 10 ms 记（少算一点，不会让长按提前触发）。
 */
void Task_Key_Scan(void *argument)
{
    uint32_t freq         = osKernelGetTickFreq();
    uint32_t period_ticks = (freq * TASK_KEY_SCAN_PERIOD_MS) / 1000u;
    uint32_t next_tick;
    uint32_t last_tick;
    uint32_t elapsed_ms;

    (void)argument;

    /* ---------- 1. 周期折算成 tick：先乘后除，低 tick 频率也不会被截成 0 ---------- */
    if (period_ticks == 0u) {
        period_ticks = 1u;
    }

    /* ---------- 2. 初始化在 MX_FREERTOS_Init() 里做完了，这里只跑节拍 ---------- */
    last_tick = osKernelGetTickCount();
    next_tick = last_tick;

    for (;;) {
        next_tick += period_ticks;

        (void)osDelayUntil(next_tick);

        /* ---------- 3. 用真实 tick 差算间隔：被抢占错过整拍时不会把时间轴拉快 ---------- */
        uint32_t now = osKernelGetTickCount();

        elapsed_ms = ((now - last_tick) * 1000u) / freq;
        last_tick  = now;

        /* 落后超过一个周期就重新对齐，避免连续多拍空转 */
        if ((int32_t)(now - next_tick) > 0) {
            next_tick = now;
        }

        if (elapsed_ms == 0u) {
            continue;
        }

        if (elapsed_ms > 0xFFFFu) {
            elapsed_ms = 0xFFFFu;
        }

        Svc_Key_Logic_Tick((uint16_t)elapsed_ms);
    }
}
