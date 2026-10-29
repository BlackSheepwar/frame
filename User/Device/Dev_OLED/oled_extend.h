/**
 * @file oled_extend.h
 * @brief 波特律动OLED驱动(SSD1306)扩展件
 * @author 制作人: 李嘉图
 * @date
 *   创建时间: 2025-12-02
 *   更新时间: 2025-12-02
 */

#ifndef __OLED_EXTEND_H__
#define __OLED_EXTEND_H__

/*==============================================================================
 * 头文件包含
 *============================================================================*/
// 固定包含
#include <stdint.h>
// 功能包含
#include "oled.h"

#define OLED_PAGES 8   // 128x64 屏幕共 8 页

/**
 * @brief 在 OLED 上显示一个十进制整数（自动计算长度）
 * @param x OLED 横坐标（像素）
 * @param y OLED 纵坐标（像素）
 * @param num 要显示的整数
 * @param font 字体指针
 * @param color 显示颜色
 */
void OLED_PrintDecimal(uint8_t x, uint8_t y, int num,
                       const ASCIIFont *font, OLED_ColorMode color);

/**
 * @brief 局部刷新 OLED 显存
 * @param start_col 起始列 (0~127)
 * @param end_col   结束列 (0~127)
 * @param start_row 起始行 (0~63)
 * @param end_row   结束行 (0~63)
 *
 * 注意：OLED 页高为 8 像素，因此 row 会自动转换为页
 */
void OLED_ShowFrameArea(uint8_t start_col, uint8_t end_col, uint8_t start_row, uint8_t end_row);

/**
 * @brief 分步刷新全屏，每次调用刷新一页（8像素高）
 * @return 1: 本帧完成了一页刷新
 * @return 0: 全屏已刷新完一轮，本次无操作
 */
uint8_t OLED_UpdateScreenByStep(void);

#endif // __OLED_EXTEND_H__