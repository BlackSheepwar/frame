#ifndef __OLED_H__
#define __OLED_H__
/*==============================================================================
 * 头文件包含
 *============================================================================*/
// 固定包含
#include <stdint.h>
#include <string.h>
// 功能包含
#include "font.h"

/*==============================================================================
 * 定义与常量
 *============================================================================*/
/**
 * @brief OLED 显示颜色模式定义
 */
typedef enum {
  OLED_COLOR_NORMAL = 0, /**< 正常显示模式：黑底白字 */
  OLED_COLOR_REVERSED    /**< 反色显示模式：白底黑字 */
} OLED_ColorMode;

/* ================================================================
 *                      基础控制函数
 * ================================================================ */

/**
 * @brief 初始化 OLED 显示模块
 *
 * 执行 OLED 的初始化序列，包括：
 * - 复位信号输出
 * - 寄存器配置（显示方向、偏移、时钟分频等）
 * - 清屏与缓冲区初始化
 *
 * @note 本函数应在系统上电或复位后调用一次。
 */
void OLED_Init(void);

/**
 * @brief 打开 OLED 显示（退出休眠模式）
 *
 * 向 OLED 发送显示开启命令，使屏幕开始显示缓冲区内容。
 */
void OLED_DisPlay_On(void);

/**
 * @brief 关闭 OLED 显示（进入休眠模式）
 *
 * 关闭显示输出以节省功耗，缓冲区内容不会丢失。
 */
void OLED_DisPlay_Off(void);

/* ================================================================
 *                      帧缓存与像素控制
 * ================================================================ */

/**
 * @brief 创建新帧（清空显存缓冲区）
 *
 * 清空屏幕缓存，使其准备显示新的内容。
 * 通常用于绘制前的初始化步骤。
 */
void OLED_NewFrame(void);

/**
 * @brief 刷新屏幕显示
 *
 * 将当前显存缓冲区（frame buffer）内容一次性写入 OLED 模块，
 * 使屏幕显示更新后的图像。
 *
 * @note 为减少闪烁，应使用双缓冲绘制策略：
 *       所有图形先绘制到缓冲区，最后统一调用此函数刷新。
 */
void OLED_ShowFrame(void);

/**
 * @brief 设置单个像素点的显示状态
 *
 * @param x      水平坐标（像素单位）
 * @param y      垂直坐标（像素单位）
 * @param color  显示模式（正常或反色）
 *
 * @note 坐标 (0,0) 通常为左上角。
 */
void OLED_SetPixel(uint8_t x, uint8_t y, OLED_ColorMode color);

/* ================================================================
 *                      基本绘图函数
 * ================================================================ */

/**
 * @brief 绘制一条直线
 *
 * @param x1, y1 起点坐标
 * @param x2, y2 终点坐标
 * @param color   绘制颜色模式
 *
 * @details 使用 Bresenham 算法绘制像素级直线。
 */
void OLED_DrawLine(uint8_t x1, uint8_t y1, uint8_t x2, uint8_t y2, OLED_ColorMode color);

/**
 * @brief 绘制一个空心矩形
 *
 * @param x, y    左上角坐标
 * @param w, h    矩形宽度与高度
 * @param color   显示模式
 */
void OLED_DrawRectangle(uint8_t x, uint8_t y, uint8_t w, uint8_t h, OLED_ColorMode color);

/**
 * @brief 绘制一个实心矩形
 *
 * @param x, y    左上角坐标
 * @param w, h    宽度与高度
 * @param color   显示模式
 */
void OLED_DrawFilledRectangle(uint8_t x, uint8_t y, uint8_t w, uint8_t h, OLED_ColorMode color);

/**
 * @brief 绘制一个空心三角形
 *
 * @param x1, y1  第一个顶点
 * @param x2, y2  第二个顶点
 * @param x3, y3  第三个顶点
 * @param color   显示模式
 */
void OLED_DrawTriangle(uint8_t x1, uint8_t y1,
                       uint8_t x2, uint8_t y2,
                       uint8_t x3, uint8_t y3,
                       OLED_ColorMode color);

/**
 * @brief 绘制一个实心三角形
 *
 * @param x1, y1  第一个顶点
 * @param x2, y2  第二个顶点
 * @param x3, y3  第三个顶点
 * @param color   显示模式
 */
void OLED_DrawFilledTriangle(uint8_t x1, uint8_t y1,
                             uint8_t x2, uint8_t y2,
                             uint8_t x3, uint8_t y3,
                             OLED_ColorMode color);

/**
 * @brief 绘制一个空心圆
 *
 * @param x, y    圆心坐标
 * @param r       半径
 * @param color   显示模式
 */
void OLED_DrawCircle(uint8_t x, uint8_t y, uint8_t r, OLED_ColorMode color);

/**
 * @brief 绘制一个实心圆
 *
 * @param x, y    圆心坐标
 * @param r       半径
 * @param color   显示模式
 */
void OLED_DrawFilledCircle(uint8_t x, uint8_t y, uint8_t r, OLED_ColorMode color);

/**
 * @brief 绘制一个空心椭圆
 *
 * @param x, y    椭圆中心坐标
 * @param a, b    椭圆长轴与短轴长度
 * @param color   显示模式
 */
void OLED_DrawEllipse(uint8_t x, uint8_t y, uint8_t a, uint8_t b, OLED_ColorMode color);

/**
 * @brief 在指定位置绘制图像
 *
 * @param x, y    左上角显示起点
 * @param img     图片数据结构指针（包含宽高与位图数据）
 * @param color   显示模式
 *
 * @note 图片应为单色位图格式，1bit 表示一个像素。
 */
void OLED_DrawImage(uint8_t x, uint8_t y, const Image *img, OLED_ColorMode color);

/* ================================================================
 *                      字符与字符串显示
 * ================================================================ */

/**
 * @brief 显示单个 ASCII 字符
 *
 * @param x, y    字符起始显示坐标
 * @param ch      要显示的字符
 * @param font    ASCII 字体结构体指针（定义宽高和字模）
 * @param color   显示模式
 */
void OLED_PrintASCIIChar(uint8_t x, uint8_t y, char ch,
                         const ASCIIFont *font, OLED_ColorMode color);

/**
 * @brief 显示 ASCII 字符串
 *
 * @param x, y    起始坐标（第一个字符位置）
 * @param str     字符串指针（以 '\0' 结尾）
 * @param font    字体结构体指针
 * @param color   显示模式
 *
 * @note 字符超出屏幕边界会被裁剪。
 */
void OLED_PrintASCIIString(uint8_t x, uint8_t y, char *str,
                           const ASCIIFont *font, OLED_ColorMode color);

/**
 * @brief 显示多字节字符串（支持中英文）
 *
 * @param x, y    起始坐标
 * @param str     要显示的字符串
 * @param font    字体结构体指针（支持多字节编码）
 * @param color   显示模式
 *
 * @details 可结合自定义字体结构实现中文、日文等复杂字体显示。
 */
void OLED_PrintString(uint8_t x, uint8_t y, char *str,
                      const Font *font, OLED_ColorMode color);

/**
* @brief 用于存放原本不引出的函数与定义提供给扩展函数（禁止在扩展函数外的地方引用）
*/
void OLED_Send(uint8_t *data, uint8_t len);
void OLED_SendCmd(uint8_t cmd);
// OLED器件地址（7 bit 地址，未左移；BSP_IIC 内部会左移一位）
#define OLED_ADDRESS 0x3C

// OLED参数
#define OLED_PAGE 8            // OLED页数
#define OLED_ROW 8 * OLED_PAGE // OLED行数
#define OLED_COLUMN 128        // OLED列数

// 显存
extern uint8_t OLED_GRAM[OLED_PAGE][OLED_COLUMN];

#endif // __OLED_H__
