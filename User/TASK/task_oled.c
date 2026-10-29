/**
 * @file task_oled.c
 * @brief 任务层：OLED 显示线程体（周期刷新）
 * @author 李嘉图
 * @date 2026-10-23
 *
 * @note 线程本身由 CubeMX 按 frame.ioc 的 Tasks01 创建，入口名为 Task_OLED，
 *       这里提供同名强定义，覆盖 freertos.c 里生成的 __weak 空入口。
 *
 *       刷新流程：OLED_NewFrame() 清显存 → 绘制 → OLED_ShowFrame() 整屏上屏。
 *       整屏约 1.1 KB I2C 数据，400 kHz 下约 25 ms，所以刷新周期取 100 ms，
 *       不要用 1 ms 的空转节拍。
 */

/*==============================================================================
 * 头文件包含
 *============================================================================*/
// 固定包含
#include <stddef.h>
#include "FreeRTOS.h"
#include "cmsis_os2.h"
// 功能包含
#include "oled_extend.h"

/*==============================================================================
 * 接口实现
 *============================================================================*/
/**
 * @brief OLED 显示线程体
 * @param argument 未使用
 * @note 上电后先等待 OLED 稳定再初始化，之后周期刷新整屏。
 */
void Task_OLED(void *argument)
{
    int a = 0;

    (void)argument;

    osDelay(20);    // STM32 启动比 OLED 上电快，等 20 ms 再初始化
    OLED_Init();    // 配置 SSD1306、清屏并点亮

    for (;;) {
        OLED_NewFrame();
        OLED_PrintDecimal(0, 0, a, &afont16x8, OLED_COLOR_NORMAL);
        OLED_ShowFrame();

        a++;            // 每刷新一次，数字加 1
        osDelay(1);   // 刷新周期 100 ms
    }
}
