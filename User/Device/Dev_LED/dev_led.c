/**
 * @file dev_led.c
 * @brief 设备层实现：LED = BSP 引脚句柄 + 极性
 * @author Ricardo
 * @date 2026-10-25
 *
 * @note 本文件不含端口号、引脚号、模式、上下拉——那些在 BSP 层。
 *       这里只记"那个个引脚属于这个 LED、什么极性可以点亮LED灯"。
 */

/*==============================================================================
 * 头文件包含
 *============================================================================*/
// 固定包含
#include <stddef.h>
#include "dev_led.h"
#include "bsp_gpio.h"

/*==============================================================================
 * 类型定义
 *============================================================================*/
 /**
 * @brief LED 设备句柄（一个 LED 一份，可多个并存）
 */
struct dev_led{
    const bsp_gpio_t *gpio;           /* 引脚句柄（BSP 层资源） */
    uint8_t           polarity;       /* 点亮电平：0=低电平点亮，1=高电平点亮 */
};

/*==============================================================================
 * 设备句柄
 *============================================================================*/
/**
 * @brief 板载 LED 实例（PC13，低电平点亮）
 */
const dev_led_t dev_led_1 = { .gpio= &bsp_gpio_pc13,  .polarity = 0u};

/*==============================================================================
 * 接口实现
 *============================================================================*/
/**
 * @brief 按状态点亮或熄灭 LED
 * @param led   LED 设备句柄，不可为 NULL
 * @param state 目标状态：LED_ON 点亮，LED_OFF 熄灭
 * @retval 1: 已把电平写入引脚
 * @retval 0: led 或 led->gpio 为空指针，或 state 不是已知状态（此时不动作）
 * @note polarity 记的是"点亮电平"（0=低电平点亮），熄灭时取它的反。
 */
uint8_t Dev_LED_Write(const dev_led_t *led, dev_led_state_t state)
{
    uint8_t level;

    /* ---------- 句柄检查 ---------- */
    if ((led == NULL) || (led->gpio == NULL)) {
        return 0u;
    }

    /* ---------- 状态换算成引脚电平 ---------- */
    switch (state) {
    case LED_ON:
        level = led->polarity;                  /* 点亮 = polarity 本身的电平 */
        break;
    case LED_OFF:
        level = (uint8_t)(led->polarity ^ 1u);  /* 熄灭 = polarity 取反 */
        break;
    default:
        return 0u;                              /* 未知状态不动作 */
    }

    /* ---------- 写入引脚 ---------- */
    BSP_GPIO_Write(led->gpio, level);
    return 1u;
}

/**
 * @brief 翻转LED状态
 * @param led   LED 设备句柄，不可为 NULL
 * @retval 1: 已把电平写入引脚
 * @retval 0: led 或 led->gpio 为空指针（此时不动作）
 */
uint8_t Dev_LED_Toggle(const dev_led_t *led)
{
    /* ---------- 句柄检查 ---------- */
    if ((led == NULL) || (led->gpio == NULL)) {
        return 0u;
    }

    /* ---------- 翻转引脚电平 ---------- */
    BSP_GPIO_Toggle(led->gpio);
    return 1u;
}
