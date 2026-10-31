/**
 * @file dev_led_bar.c
 * @brief 设备层实现：灯带 = 一组 BSP 引脚句柄 + 极性
 * @author Ricardo
 * @date 2026-10-23
 *
 * @note 本文件不含端口号、引脚号、模式、上下拉——那些在 BSP 层。
 *       这里只记"哪几个引脚属于这条灯带、按什么顺序点亮、什么极性"。
 */

/*==============================================================================
 * 头文件包含
 *============================================================================*/
// 固定包含
#include <stddef.h>
#include "dev_led_bar.h"
#include "bsp_gpio.h"

/*==============================================================================
 * 类型定义
 *============================================================================*/
/**
 * @brief 灯带设备句柄（一台灯带一份，可多个并存）
 */
struct dev_led_bar{
    const bsp_gpio_t *const *pins;      /* 每颗灯一个引脚句柄，顺序即点亮顺序 */
    uint8_t                  count;     /* 灯珠数量 */
    uint8_t                  polarity;  /* 0: 低电平点亮，1: 高电平点亮 */
};

/*==============================================================================
 * 设备句柄
 *============================================================================*/
/**
 * @brief 灯带引脚顺序表：第 0 颗在 PA0，第 7 颗在 PA7
 */
static const bsp_gpio_t *const s_dev_led_pins[] = {
    &bsp_gpio_pa0, &bsp_gpio_pa1, &bsp_gpio_pa2, &bsp_gpio_pa3,
    &bsp_gpio_pa4, &bsp_gpio_pa5, &bsp_gpio_pa6, &bsp_gpio_pa7,
};

/**
 * @brief 8 颗 LED 进度条（高电平点亮）
 */
const dev_led_bar_t dev_led_bar_0 = {
    .pins       = s_dev_led_pins,
    .count      = 8u,
    .polarity = 1u,
};

/*==============================================================================
 * 接口实现
 *============================================================================*/
/**
 * @brief 点亮前 lit 颗灯，其余熄灭
 * @param bar 灯带设备句柄
 * @param lit 点亮颗数，超出范围自动夹紧到 [0, Dev_LED_Bar_Count(bar)]
 */
void Dev_LED_Bar_Set(const dev_led_bar_t *bar, uint8_t lit)
{
    uint8_t index;
    uint8_t on_level;
    uint8_t off_level;

    if ((bar == NULL) || (bar->pins == NULL)) {
        return;
    }

    /* ---------- 1. 超出灯珠数量就夹紧 ---------- */
    if (lit > bar->count) {
        lit = bar->count;
    }

    /* ---------- 2. 极性只在入口算一次，循环里不再判断 ---------- */
    on_level  = bar->polarity ? 1u : 0u;
    off_level = (uint8_t)(on_level ^ 1u);

    /* ---------- 3. 逐颗写引脚句柄 ---------- */
    for (index = 0u; index < bar->count; ++index) {
        BSP_GPIO_Write(bar->pins[index], 
                       (index < lit) ? on_level : off_level);
    }
}

/**
 * @brief 查询灯带灯珠数量
 * @param bar 灯带设备句柄，不能为 NULL
 * @retval 非 0: 该灯带的灯珠数量
 * @retval 0: bar 为 NULL，或灯带本身没有配置灯珠
 * @note 只读，不改变任何引脚电平，可在任务上下文任意调用。
 */
uint8_t Dev_LED_Bar_Count(const dev_led_bar_t *bar)
{
    if (bar == NULL) {
        return 0u;
    }

    return bar->count;
}
