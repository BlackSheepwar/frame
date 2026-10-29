/**
 * @file oled_extend.c
 * @brief 波特律动OLED驱动(SSD1306)扩展件
 * @author 制作人: 李嘉图
 * @dateh
 *   创建时间: 2025-12-02
 *   更新时间: 2025-12-02
 */

/*==============================================================================
 * 头文件包含
 *============================================================================*/
// 固定包含
#include <stdint.h>
#include <stdio.h>
#include "oled_extend.h"
// 功能包含
#include "oled.h"

static uint8_t update_page = 0;  // 当前刷新到哪一页

/**
 * @brief  在 OLED 上显示一个十进制整数（支持负数，不依赖 snprintf）
 * @param  x     OLED 横坐标（像素）
 * @param  y     OLED 纵坐标（像素）
 * @param  num   要显示的整数（可为负数）
 * @param  font  字体指针
 * @param  color 显示颜色
 */
void OLED_PrintDecimal(uint8_t x, uint8_t y, int num,
                       const ASCIIFont *font, OLED_ColorMode color)
{
    char buf[12];           // -2147483648 需要 11 个字符 + '\0'
    int i = 0;              // 写入位置
    int is_negative = 0;

    // 1. 处理负号，并安全处理 INT_MIN
    if (num < 0) {
        is_negative = 1;
        if (num == -2147483647 - 1) {   // INT_MIN，不能直接取反
            buf[i++] = '8';
            num = 214748364;
        } else {
            num = -num;
        }
    }

    // 2. 转换数字部分（倒序放入 buf，从 i 开始）
    int digit_start = i;                // 数字部分的起始索引
    if (num == 0) {
        buf[i++] = '0';
    } else {
        while (num > 0) {
            buf[i++] = (num % 10) + '0';
            num /= 10;
        }
    }

    // 3. 反转数字部分（因为是个、十、百...倒序存放的）
    int left = digit_start;
    int right = i - 1;
    while (left < right) {
        char tmp = buf[left];
        buf[left] = buf[right];
        buf[right] = tmp;
        left++;
        right--;
    }

    // 4. 如果是负数，在最前面插入 '-'
    if (is_negative) {
        // 整体后移一位
        for (int j = i; j >= 0; j--) {
            buf[j + 1] = buf[j];
        }
        buf[0] = '-';
        i++;
    }

    buf[i] = '\0';  // 字符串结尾

    // 5. 调用你已有的字符串绘制函数
    OLED_PrintASCIIString(x, y, buf, font, color);
}

/**
 * @brief 局部刷新 OLED 显存
 * @param start_col 起始列 (0~127)
 * @param end_col   结束列 (0~127)
 * @param start_row 起始行 (0~63)
 * @param end_row   结束行 (0~63)
 *
 * 注意：OLED 页高为 8 像素，因此 row 会自动转换为页
 */
void OLED_ShowFrameArea(uint8_t start_col, uint8_t end_col, uint8_t start_row, uint8_t end_row)
{
    if (start_col >= OLED_COLUMN) start_col = OLED_COLUMN - 1;
    if (end_col >= OLED_COLUMN) end_col = OLED_COLUMN - 1;
    if (start_row >= OLED_ROW) start_row = OLED_ROW - 1;
    if (end_row >= OLED_ROW) end_row = OLED_ROW - 1;

    uint8_t start_page = start_row / 8;
    uint8_t end_page   = end_row   / 8;
    static uint8_t sendBuffer[OLED_COLUMN + 1];
    sendBuffer[0] = 0x40;

    for (uint8_t page = start_page; page <= end_page; page++)
    {
        OLED_SendCmd(0xB0 + page);          // 页地址
        OLED_SendCmd(0x00 + (start_col & 0x0F));  // 列低4位
        OLED_SendCmd(0x10 + (start_col >> 4));    // 列高4位

        uint8_t len = end_col - start_col + 1;
        memcpy(sendBuffer + 1, &OLED_GRAM[page][start_col], len);
        OLED_Send(sendBuffer, len + 1);
    }
}

/**
 * @brief 分步刷新全屏，每次调用刷新一页（8像素高）
 * @return 1: 本帧完成了一页刷新
 * @return 0: 全屏已刷新完一轮，本次无操作
 */
uint8_t OLED_UpdateScreenByStep(void)
{
    if (update_page < OLED_PAGES) {
        // 刷新第 update_page 页的全部 128 列
        OLED_ShowFrameArea(0, 127, update_page * 8, update_page * 8 + 7);
        update_page++;
        return 1;  // 正在刷新
    } else {
        // 本轮全屏刷新完毕，重置状态，可开始新一轮
        update_page = 0;
        return 0;  // 本轮完成
    }
}
