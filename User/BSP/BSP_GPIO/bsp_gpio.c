/**
 * @file bsp_gpio.c
 * @brief 硬件驱动层实现：把句柄翻译成 HAL 的端口与引脚
 * @author Ricardo
 * @date 2026-10-23
 *
 * @note 本文件只做三件事：判空、把句柄字段喂给 HAL、返回结果。
 *       引脚的方向与上下拉由 CubeMX 生成的 MX_GPIO_Init() 配置，
 *       这里不重复初始化硬件。
 *
 *       头文件里端口是不透明的 void *，真正的 GPIO_TypeDef * 只在
 *       本文件还原，HAL 因此不会泄漏到上层。
 */

/*==============================================================================
 * 头文件包含
 *============================================================================*/
// 固定包含
#include <stddef.h>
#include "stm32f1xx_hal.h"
#include "bsp_gpio.h"

/*==============================================================================
 * 类型定义
 *============================================================================*/
/**
 * @brief GPIO 句柄（一个引脚一份）
 * @note 端口、引脚号、方向三个字段；上下拉仍属于硬件配置，不放在句柄里。
 *       port 是 `GPIO_TypeDef *` 的不透明指针，只有 bsp_gpio.c 知道真实类型。
 */
struct bsp_gpio {
    void          *port;                /* 端口基址（bsp_gpio.c 内还原成 GPIO_TypeDef *） */
    uint16_t       pin;                 /* 引脚掩码（GPIO_PIN_x） */
    bsp_gpio_dir_t dir;                 /* 方向：见 bsp_gpio_dir_t */
};

/*==============================================================================
 * 句柄实例：端口、引脚号、方向只出现在这里
 *============================================================================*/
// LED灯条
const bsp_gpio_t bsp_gpio_pa0 = { .port = GPIOA, .pin = GPIO_PIN_0, .dir = BSP_GPIO_DIR_OUT };
const bsp_gpio_t bsp_gpio_pa1 = { .port = GPIOA, .pin = GPIO_PIN_1, .dir = BSP_GPIO_DIR_OUT };
const bsp_gpio_t bsp_gpio_pa2 = { .port = GPIOA, .pin = GPIO_PIN_2, .dir = BSP_GPIO_DIR_OUT };
const bsp_gpio_t bsp_gpio_pa3 = { .port = GPIOA, .pin = GPIO_PIN_3, .dir = BSP_GPIO_DIR_OUT };
const bsp_gpio_t bsp_gpio_pa4 = { .port = GPIOA, .pin = GPIO_PIN_4, .dir = BSP_GPIO_DIR_OUT };
const bsp_gpio_t bsp_gpio_pa5 = { .port = GPIOA, .pin = GPIO_PIN_5, .dir = BSP_GPIO_DIR_OUT };
const bsp_gpio_t bsp_gpio_pa6 = { .port = GPIOA, .pin = GPIO_PIN_6, .dir = BSP_GPIO_DIR_OUT };
const bsp_gpio_t bsp_gpio_pa7 = { .port = GPIOA, .pin = GPIO_PIN_7, .dir = BSP_GPIO_DIR_OUT };
// 单个LED灯
const bsp_gpio_t bsp_gpio_pc13 = { .port = GPIOC, .pin = GPIO_PIN_13, .dir = BSP_GPIO_DIR_OUT };
// 按键
const bsp_gpio_t bsp_gpio_pb1  = { .port = GPIOB, .pin = GPIO_PIN_1, .dir = BSP_GPIO_DIR_IN };
const bsp_gpio_t bsp_gpio_pb10 = { .port = GPIOB, .pin = GPIO_PIN_10, .dir = BSP_GPIO_DIR_IN };
const bsp_gpio_t bsp_gpio_pb11 = { .port = GPIOB, .pin = GPIO_PIN_11, .dir = BSP_GPIO_DIR_IN };
// SPI2 片选（LCD）
const bsp_gpio_t bsp_gpio_pa8  = { .port = GPIOA, .pin = GPIO_PIN_8, .dir = BSP_GPIO_DIR_OUT };

/*==============================================================================
 * 接口实现
 *============================================================================*/
/**
 * @brief 读取引脚电平
 * @param gpio 引脚句柄
 * @retval 1: 高电平
 * @retval 0: 低电平
 * @note 输出引脚读回来的是引脚实际电平（可能被外部拉低），不是 ODR 寄存器的值。
 */
uint8_t BSP_GPIO_Read(const bsp_gpio_t *gpio)
{
    GPIO_TypeDef *port;

    if (gpio == NULL) {
        return 0u;
    }

    port = (GPIO_TypeDef *)gpio->port;

    if (port == NULL) {
        return 0u;
    }

    return (HAL_GPIO_ReadPin(port, gpio->pin) == GPIO_PIN_SET) ? 1u : 0u;
}

/**
 * @brief 设置引脚输出电平
 * @param gpio  引脚句柄
 * @param level 1 为高，0 为低
 * @note 输入句柄直接忽略：写输入寄存器不会产生任何电平变化，
 *       悄悄"成功"会让上层以为灯亮了，所以这里干脆挡掉。
 */
void BSP_GPIO_Write(const bsp_gpio_t *gpio, uint8_t level)
{
    GPIO_TypeDef *port;

    if ((gpio == NULL) || (gpio->dir == BSP_GPIO_DIR_IN)) {
        return;
    }

    port = (GPIO_TypeDef *)gpio->port;

    if (port == NULL) {
        return;
    }

    HAL_GPIO_WritePin(port, gpio->pin, (level != 0u) ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

/**
 * @brief 翻转引脚输出电平
 * @param gpio 引脚句柄
 * @note 输入句柄同样忽略。
 */
void BSP_GPIO_Toggle(const bsp_gpio_t *gpio)
{
    GPIO_TypeDef *port;

    if ((gpio == NULL) || (gpio->dir == BSP_GPIO_DIR_IN)) {
        return;
    }

    port = (GPIO_TypeDef *)gpio->port;

    if (port == NULL) {
        return;
    }

    HAL_GPIO_TogglePin(port, gpio->pin);
}
