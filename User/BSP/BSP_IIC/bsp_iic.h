/**
 * @file bsp_iic.h
 * @brief IIC 硬件驱动层：互斥量排队 + DMA 完成中断唤醒
 * @author Ricardo
 * @date 2026-10-27
 *
 * @note 一个 bsp_iic_t 对应一路硬件 IIC。一次传输由 BSP_IIC_Transfer()
 *       同步完成：抢总线互斥量（排队）→ 启动 DMA → 阻塞等完成中断
 *       唤醒 → 返回结果。公共头文件不暴露 HAL 类型，HAL 句柄只存在
 *       于 .c 内部。
 *       互斥量 IIC_LOCK 配在 frame.ioc 中，由 freertos.c 创建，
 *       本层只负责绑定。
 */

#ifndef __BSP_IIC_H__
#define __BSP_IIC_H__

/*==============================================================================
 * 头文件包含
 *============================================================================*/
// 固定包含
#include <stdint.h>

/*==============================================================================
 * 类型定义
 *============================================================================*/
/**
 * @brief 传输方向
 */
typedef enum {
    BSP_IIC_DIR_TX = 0, /* 主机发送 */
    BSP_IIC_DIR_RX = 1, /* 主机接收 */
} bsp_iic_dir_t;

/**
 * @brief 传输模式
 */
typedef enum {
    BSP_IIC_MODE_RAW = 0, /* 直接收发，没有寄存器子地址 */
    BSP_IIC_MODE_MEM = 1, /* 带寄存器子地址，repeated START 由 HAL 完成 */
} bsp_iic_mode_t;

/**
 * @brief BSP_IIC 返回码
 */
typedef enum {
    BSP_IIC_OK          = 0,  /* 成功 */
    BSP_IIC_ERR_PARAM   = -1, /* 参数非法 */
    BSP_IIC_ERR_STATE   = -2, /* 驱动未初始化 */
    BSP_IIC_ERR_TIMEOUT = -3, /* 排队等锁超时，或传输超时 */
    BSP_IIC_ERR_NACK    = -4, /* 从机 NACK */
    BSP_IIC_ERR_BUS     = -5, /* 总线错误 */
    BSP_IIC_ERR_HAL     = -7, /* HAL 启动失败或其他 HAL 错误 */
} bsp_iic_status_t;

/**
 * @brief 一次传输命令
 */
typedef struct {
    uint16_t dev_addr;      /* 7 bit 从机地址，未左移 */
    uint16_t mem_addr;      /* 寄存器子地址，RAW 模式忽略 */
    uint16_t mem_addr_size; /* 子地址字节数 1 或 2，RAW 模式忽略 */
    uint16_t len;           /* 数据字节数，不能为 0 */
    uint8_t  dir;           /* 方向，取 bsp_iic_dir_t */
    uint8_t  mode;          /* 模式，取 bsp_iic_mode_t */
    uint8_t *data;          /* 收发缓冲区，不能为 NULL */
    uint32_t timeout_ms;    /* 超时，0 表示用默认 50 ms */
} bsp_iic_msg_t;

/**
 * @brief IIC 总线句柄（不透明类型，成员定义在 bsp_iic.c）
 */
typedef struct bsp_iic bsp_iic_t;

/*==============================================================================
 * 外部变量
 *============================================================================*/
extern bsp_iic_t bsp_iic1;

/*==============================================================================
 * 函数声明
 *============================================================================*/
/**
 * @brief 初始化 IIC 驱动：绑定 CubeMX 创建的互斥量
 * @retval BSP_IIC_OK: 初始化成功（重复调用直接返回成功）
 * @retval BSP_IIC_ERR_STATE: 互斥量句柄缺失
 * @note 必须在 freertos.c 的 USER CODE BEGIN RTOS_MUTEX 中调用：
 *       此时互斥量已创建、MX_I2C1_Init() 也已执行。
 */
bsp_iic_status_t BSP_IIC_Init(void);

/**
 * @brief 提交一次 IIC 传输并阻塞等待完成
 * @param bus 总线句柄，不能为 NULL
 * @param msg 传输命令，不能为 NULL；data 在返回前必须保持有效
 * @retval BSP_IIC_OK: 传输成功
 * @retval BSP_IIC_ERR_PARAM: 地址、方向、模式、长度或缓冲区非法
 * @retval BSP_IIC_ERR_STATE: 驱动未初始化
 * @retval BSP_IIC_ERR_TIMEOUT: 排队等锁超时，或传输超时
 * @retval BSP_IIC_ERR_NACK: 从机 NACK
 * @retval BSP_IIC_ERR_BUS: 总线错误
 * @retval BSP_IIC_ERR_HAL: HAL 启动失败或其他 HAL 错误
 * @note 不能在中断中调用，也不能被同一任务重入。
 */
bsp_iic_status_t BSP_IIC_Transfer(bsp_iic_t *bus, const bsp_iic_msg_t *msg);

/**
 * @brief 直接向从机发送数据（RAW 模式）
 * @param bus      总线句柄，不能为 NULL
 * @param dev_addr 7 bit 从机地址，未左移
 * @param data     发送缓冲区，不能为 NULL
 * @param len      发送字节数，不能为 0
 * @retval BSP_IIC_OK: 发送成功
 * @retval BSP_IIC_ERR_PARAM: 参数非法
 * @retval BSP_IIC_ERR_STATE: 驱动未初始化
 * @retval BSP_IIC_ERR_TIMEOUT: 排队或传输超时
 * @retval BSP_IIC_ERR_NACK: 从机 NACK
 * @retval BSP_IIC_ERR_BUS: 总线错误
 * @retval BSP_IIC_ERR_HAL: HAL 错误
 */
bsp_iic_status_t BSP_IIC_Write(bsp_iic_t *bus, uint16_t dev_addr,
                               const uint8_t *data, uint16_t len);

/**
 * @brief 直接从从机读取数据（RAW 模式）
 * @param bus      总线句柄，不能为 NULL
 * @param dev_addr 7 bit 从机地址，未左移
 * @param data     接收缓冲区，不能为 NULL
 * @param len      接收字节数，不能为 0
 * @retval BSP_IIC_OK: 读取成功
 * @retval BSP_IIC_ERR_PARAM: 参数非法
 * @retval BSP_IIC_ERR_STATE: 驱动未初始化
 * @retval BSP_IIC_ERR_TIMEOUT: 排队或传输超时
 * @retval BSP_IIC_ERR_NACK: 从机 NACK
 * @retval BSP_IIC_ERR_BUS: 总线错误
 * @retval BSP_IIC_ERR_HAL: HAL 错误
 */
bsp_iic_status_t BSP_IIC_Read(bsp_iic_t *bus, uint16_t dev_addr,
                              uint8_t *data, uint16_t len);

/**
 * @brief 向从机的 8 bit 子地址写数据（MEM 模式）
 * @param bus      总线句柄，不能为 NULL
 * @param dev_addr 7 bit 从机地址，未左移
 * @param reg      8 bit 寄存器子地址
 * @param data     发送缓冲区，不能为 NULL
 * @param len      发送字节数，不能为 0
 * @retval BSP_IIC_OK: 写入成功
 * @retval BSP_IIC_ERR_PARAM: 参数非法
 * @retval BSP_IIC_ERR_STATE: 驱动未初始化
 * @retval BSP_IIC_ERR_TIMEOUT: 排队或传输超时
 * @retval BSP_IIC_ERR_NACK: 从机 NACK
 * @retval BSP_IIC_ERR_BUS: 总线错误
 * @retval BSP_IIC_ERR_HAL: HAL 错误
 */
bsp_iic_status_t BSP_IIC_WriteReg(bsp_iic_t *bus, uint16_t dev_addr,
                                  uint8_t reg, const uint8_t *data,
                                  uint16_t len);

/**
 * @brief 从从机的 8 bit 子地址读数据（MEM 模式）
 * @param bus      总线句柄，不能为 NULL
 * @param dev_addr 7 bit 从机地址，未左移
 * @param reg      8 bit 寄存器子地址
 * @param data     接收缓冲区，不能为 NULL
 * @param len      接收字节数，不能为 0
 * @retval BSP_IIC_OK: 读取成功
 * @retval BSP_IIC_ERR_PARAM: 参数非法
 * @retval BSP_IIC_ERR_STATE: 驱动未初始化
 * @retval BSP_IIC_ERR_TIMEOUT: 排队或传输超时
 * @retval BSP_IIC_ERR_NACK: 从机 NACK
 * @retval BSP_IIC_ERR_BUS: 总线错误
 * @retval BSP_IIC_ERR_HAL: HAL 错误
 */
bsp_iic_status_t BSP_IIC_ReadReg(bsp_iic_t *bus, uint16_t dev_addr,
                                 uint8_t reg, uint8_t *data,
                                 uint16_t len);

#endif /* __BSP_IIC_H__ */
