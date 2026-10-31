/**
 * @file bsp_spi.h
 * @brief SPI 硬件驱动层：总线句柄 + 片选设备句柄，互斥量排队 + 完成唤醒
 * @author Ricardo
 * @date 2026-10-29
 *
 * @note 两级句柄，对应"一条总线挂多个设备"：
 *       bsp_spi_t     —— 一路 SPI 外设，一条总线一把互斥量，负责排队；
 *       bsp_spi_dev_t —— 挂在该总线上的一路片选，一个设备一个句柄。
 *       设备级函数自动拉/放 CS，总线级函数只管搬字节，CS 由调用者负责。
 *
 *       一次传输同步完成：抢互斥量（排队）→ 启动传输 → 阻塞等完成唤醒
 *       → 返回结果。外设配了 DMA 就走 DMA + 任务通知；没配 DMA 的外设
 *       自动退回 HAL 阻塞接口，所以同一个模块在任意一路 SPI 上都能用。
 *
 *       方向能力由 CubeMX 里的 Direction 决定（2LINES / 1LINE /
 *       2LINES_RXONLY），不支持的组合在传输入口直接返回 ERR_MODE，
 *       不会把错误藏到 HAL 里。本层只服务主机模式：CubeMX 里 Mode 必须
 *       是 Master，配成 Slave 的实例会在 BSP_SPI_Init() 被拒。
 *
 *       互斥量 SPI_LOCK 配在 frame.ioc，由 freertos.c 创建，本层只负责
 *       绑定。公共头文件不暴露 HAL 类型，HAL 句柄只存在于 .c 内部。
 */

#ifndef __BSP_SPI_H__
#define __BSP_SPI_H__

/*==============================================================================
 * 头文件包含
 *============================================================================*/
// 固定包含
#include <stdint.h>

/*==============================================================================
 * 类型定义
 *============================================================================*/
/**
 * @brief BSP_SPI 返回码
 */
typedef enum {
    BSP_SPI_OK          = 0,  /* 成功 */
    BSP_SPI_ERR_PARAM   = -1, /* 参数非法 */
    BSP_SPI_ERR_STATE   = -2, /* 驱动未初始化，或外设状态不对 */
    BSP_SPI_ERR_TIMEOUT = -3, /* 排队等锁超时，或传输超时 */
    BSP_SPI_ERR_MODE    = -4, /* 外设当前配置不支持该方向 */
    BSP_SPI_ERR_BUS     = -5, /* 总线错误：MODF / OVR / CRC */
    BSP_SPI_ERR_HAL     = -7, /* HAL 启动失败或 DMA 错误 */
} bsp_spi_status_t;

/**
 * @brief SPI 总线句柄（不透明类型，成员定义在 bsp_spi.c）
 */
typedef struct bsp_spi bsp_spi_t;

/**
 * @brief SPI 设备句柄（不透明类型，成员定义在 bsp_spi.c）
 * @note 只描述"挂在哪条总线上、用哪根片选、什么极性"，不含引脚号。
 */
typedef struct bsp_spi_dev bsp_spi_dev_t;

/*==============================================================================
 * 外部变量
 *============================================================================*/
extern bsp_spi_t bsp_spi2;
extern const bsp_spi_dev_t bsp_spi2_dev_lcd;

/*==============================================================================
 * 函数声明
 *============================================================================*/
/**
 * @brief 初始化 SPI 驱动：轮询总线表，登记句柄与方向能力
 * @retval BSP_SPI_OK: 初始化成功（已初始化的总线会被跳过）
 * @retval BSP_SPI_ERR_STATE: 某一路缺 HAL 句柄、互斥量未创建、外设没回到
 *         READY、Mode 不是 Master，或数据宽度不是 8 bit
 * @note 互斥量由 CubeMX 在 freertos.c 中创建、在实例定义处绑定，所以必须
 *       等 MX_FREERTOS_Init() 建完互斥量再调用（USER CODE BEGIN
 *       RTOS_MUTEX 内）；其余条件是 MX_SPIx_Init() 已经执行过。
 */
bsp_spi_status_t BSP_SPI_Init(void);

/**
 * @brief 在指定总线上提交一次传输并阻塞等待完成（不管片选）
 * @param bus        总线句柄，不能为 NULL
 * @param tx         发送缓冲区，NULL 表示本次不发
 * @param rx         接收缓冲区，NULL 表示本次不收
 * @param len        传输字节数，不能为 0
 * @param timeout_ms 超时预算（毫秒），0 表示用默认 50 ms
 * @retval BSP_SPI_OK: 传输成功
 * @retval BSP_SPI_ERR_PARAM: 句柄或长度非法，或两个缓冲区都为 NULL
 * @retval BSP_SPI_ERR_STATE: 驱动未初始化
 * @retval BSP_SPI_ERR_TIMEOUT: 排队等锁超时，或传输超时
 * @retval BSP_SPI_ERR_MODE: 外设当前方向不支持本次传输
 * @retval BSP_SPI_ERR_BUS: 总线错误
 * @retval BSP_SPI_ERR_HAL: HAL 启动失败或 DMA 错误
 * @note 方向不用显式给，由两个指针推出来：
 *         tx 非空 + rx 为空  → 只发
 *         tx 为空 + rx 非空  → 只收
 *         两个都非空        → 全双工
 *         两个都为空        → ERR_PARAM
 *       本函数不管片选：单设备常选、或需要自己控制片选保持时用它；常规
 *       带片选的收发用 BSP_SPI_Dev_Transfer()。多任务可并发调用，按互斥量
 *       排队。不能在中断中调用，也不能被同一任务重入。timeout_ms 同时用于
 *       排队和传输，两者共用一份预算；超时后外设会被中止复位，总线随即
 *       交给下一个排队的任务。
 */
bsp_spi_status_t BSP_SPI_Transfer(bsp_spi_t *bus, const uint8_t *tx,
                                  uint8_t *rx, uint16_t len,
                                  uint32_t timeout_ms);

/**
 * @brief 拉有效片选
 * @param dev 设备句柄，不能为 NULL
 * @note 电平按句柄里的 cs_active_level 换算，调用者不用关心极性。
 *       常规单次收发不需要直接调用，BSP_SPI_Dev_Transfer() 会自己做；
 *       需要一帧内分多段发、片选全程保持低时，用它自己成对包住若干次
 *       BSP_SPI_Transfer()。
 */
void BSP_SPI_Dev_Select(const bsp_spi_dev_t *dev);

/**
 * @brief 放无效片选
 * @param dev 设备句柄，不能为 NULL
 * @note 与 BSP_SPI_Dev_Select() 配对，用于一帧分多段发、片选全程保持低
 *       的场景。
 */
void BSP_SPI_Dev_Deselect(const bsp_spi_dev_t *dev);

/**
 * @brief 在指定设备上提交一次传输，自动拉放片选
 * @param dev        设备句柄，不能为 NULL
 * @param tx         发送缓冲区，NULL 表示本次不发
 * @param rx         接收缓冲区，NULL 表示本次不收
 * @param len        传输字节数，不能为 0
 * @param timeout_ms 超时预算（毫秒），0 表示用默认 50 ms
 * @retval BSP_SPI_OK: 传输成功
 * @retval BSP_SPI_ERR_PARAM: 句柄或长度非法，或两个缓冲区都为 NULL
 * @retval BSP_SPI_ERR_STATE: 驱动未初始化
 * @retval BSP_SPI_ERR_TIMEOUT: 排队或传输超时
 * @retval BSP_SPI_ERR_MODE: 外设当前方向不支持本次传输
 * @retval BSP_SPI_ERR_BUS: 总线错误
 * @retval BSP_SPI_ERR_HAL: HAL 错误
 * @note 方向推断规则与 BSP_SPI_Transfer() 完全一致。片选在传输前拉有效、
 *       返回前放无效，成功失败都放。需要片选跨多次传输保持低，改用
 *       BSP_SPI_Dev_Select() + BSP_SPI_Transfer() 自己成对包住。
 */
bsp_spi_status_t BSP_SPI_Dev_Transfer(const bsp_spi_dev_t *dev,
                                      const uint8_t *tx, uint8_t *rx,
                                      uint16_t len, uint32_t timeout_ms);

#endif /* __BSP_SPI_H__ */
