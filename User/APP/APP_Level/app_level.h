/**
 * @file app_level.h
 * @brief 应用层：档位业务
 * @author 李嘉图
 * @date 2026-10-22
 *
 * @note APP 层只做业务，不建线程、不碰内核、不驱动下层服务：
 *       - CubeMX 负责建线程（.ioc 里配），线程体在 User/TASK 下；
 *       - TASK 的线程体可以直接调任何下层（Svc → Dev → BSP），
 *         所以按键扫描节拍由 Task_Key_Scan 调 Svc_Key_Logic_Tick()，
 *         这里不提供转包函数；
 *       - 按键事件到动作的映射在 app_key_cmd.c，不在这里。
 *
 *       调用顺序：APP_Level_Init() 必须早于 osKernelStart()（由 freertos.c 调用），
 *       APP_Level_Step() 只在调度器启动后的命令线程里调用。
 */

#ifndef __APP_LEVEL_H__
#define __APP_LEVEL_H__

/*==============================================================================
 * 头文件包含
 *============================================================================*/
// 固定包含
#include <stdint.h>

/*==============================================================================
 * 函数声明
 *============================================================================*/
/**
 * @brief 一次性初始化：显示归零
 * @note 由 Core/Src/freertos.c 的 MX_FREERTOS_Init() 在调度器启动前调用。
 */
void APP_Level_Init(void);

/**
 * @brief 档位步进（带回绕）
 * @param delta +1 或 -1
 * @note 由 app_key_cmd.c 的命令表 handler 调用；
 *       0 → 1 → … → 8 → 0，反向同理。
 */
void APP_Level_Step(int8_t delta);

#endif /* __APP_LEVEL_H__ */
