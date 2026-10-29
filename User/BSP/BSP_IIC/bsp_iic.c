/**
 * @file bsp_iic.c
 * @brief IIC 硬件驱动层：互斥量排队 + 完成中断唤醒
 * @author 李嘉图
 * @date 2026-10-27
 *
 * @note 设计模型：
 *       1. 每个 bsp_iic_t 实例绑一把互斥量，任务先抢锁再启动传输；
 *          抢不到就在锁上排队，排队深度等于任务数，没有固定上限；
 *       2. 传输用 HAL 的 DMA 接口启动，启动调用一进去就返回；
 *       3. 完成/错误中断只做两件事：记录结果、唤醒占着总线的任务；
 *       4. 调用者睡在任务通知上，被中断直接唤醒，全程不做轮询。
 *
 *       OS 对象归属：互斥量 IIC_LOCK 定义在 frame.ioc，由 CubeMX 生成到
 *       freertos.c；本文件只 extern 使用，不创建任务、队列或同步对象。
 *       互斥量带优先级继承，所以 BSP_IIC_Init() 必须放在 freertos.c 的
 *       USER CODE BEGIN RTOS_MUTEX，那里 IIC_LOCKHandle 已经建好。
 *
 *       链接注意：HAL 回调是强符号，必须保证 bsp_iic.c.o 被拉进最终
 *       镜像；freertos.c 的 BSP_IIC_Init() 调用会完成这件事。
 */

/*==============================================================================
 * 头文件包含
 *============================================================================*/
// 固定包含
#include <stddef.h>
#include <stdint.h>
#include "bsp_iic.h"
// 功能包含
#include "i2c.h"
#include "FreeRTOS.h"
#include "task.h"
#include "cmsis_os.h"

/*==============================================================================
 * 外部变量声明
 *============================================================================*/
/** @brief CubeMX 生成的互斥量句柄（frame.ioc 里配置的 IIC_LOCK） */
extern osMutexId_t IIC_LOCKHandle;

/*==============================================================================
 * 宏定义与常量
 *============================================================================*/
/** @brief 单笔传输默认超时（ms），排队与总线传输共用这一份预算 */
#define BSP_IIC_DEFAULT_TIMEOUT_MS  50U

/*==============================================================================
 * 类型定义
 *============================================================================*/
/**
 * @brief IIC 总线对象
 * @note  hal 是 void *，避免公共头文件引入 HAL 类型；lock 存的是互斥量
 *        句柄的地址——句柄值要等 freertos.c 运行时才产生，存地址就能在
 *        实例定义处一次配好，初始化时只做校验。
 */
struct bsp_iic {
    void             *hal;        /* HAL I2C 句柄（I2C_HandleTypeDef *） */
    osMutexId_t      *lock;       /* 指向 freertos.c 生成的互斥量句柄 */
    TaskHandle_t volatile waiter; /* 正在等完成的任务，空闲为 NULL */
    volatile bsp_iic_status_t result; /* 本次传输结果，由中断写入 */
    volatile uint8_t  initialized; /* 1: 已完成初始化 */
};

/*==============================================================================
 * 内部变量
 *============================================================================*/
/** @brief I2C1 总线对象；hal/lock 都指向 CubeMX 生成的句柄 */
bsp_iic_t bsp_iic1 = {
    .hal         = &hi2c1,
    .lock        = &IIC_LOCKHandle,
    .waiter      = NULL,
    .result      = 0,
    .initialized = 0U,
};

/** @brief 驱动管理的总线表；增加 I2C2 时只在这里追加实例，
 *         初始化和中断反查都会自动轮询到它 */
static bsp_iic_t *const s_buses[] = {
    &bsp_iic1,
};

/*==============================================================================
 * 内部函数
 *============================================================================*/
/**
 * @brief 按 HAL 句柄查找对应的 BSP 总线对象
 * @param hi2c HAL IIC 句柄
 * @retval 非 NULL: 对应的总线对象
 * @retval NULL: 不是本驱动管理的句柄
 */
static bsp_iic_t *BSP_IIC_FindBus(I2C_HandleTypeDef *hi2c)
{
    size_t i;

    if (hi2c == NULL) {
        return NULL;
    }

    for (i = 0U; i < sizeof(s_buses) / sizeof(s_buses[0]); i++) {
        if (s_buses[i]->hal == (void *)hi2c) {
            return s_buses[i];
        }
    }
    return NULL;
}

/**
 * @brief 按命令模式启动一次 HAL DMA 传输
 * @param bus 总线对象
 * @param msg 传输命令
 * @retval HAL_OK: 启动成功
 * @retval 其他: HAL 启动失败
 * @note 7 bit 地址在这里左移成 HAL 需要的 8 bit 形式；MEM 模式用
 *       HAL_I2C_Mem_*_DMA，一次调用完成子地址写 + repeated START。
 */
static HAL_StatusTypeDef BSP_IIC_StartHal(bsp_iic_t *bus,
                                          const bsp_iic_msg_t *msg)
{
    I2C_HandleTypeDef *hi2c = (I2C_HandleTypeDef *)bus->hal;
    uint16_t dev_addr = (uint16_t)(msg->dev_addr << 1);
    uint16_t mem_size;

    if (msg->mode == BSP_IIC_MODE_MEM) {
        mem_size = (msg->mem_addr_size == 2U) ?
                   I2C_MEMADD_SIZE_16BIT : I2C_MEMADD_SIZE_8BIT;
        if (msg->dir == BSP_IIC_DIR_TX) {
            return HAL_I2C_Mem_Write_DMA(hi2c, dev_addr, msg->mem_addr,
                                         mem_size, msg->data, msg->len);
        }
        return HAL_I2C_Mem_Read_DMA(hi2c, dev_addr, msg->mem_addr,
                                    mem_size, msg->data, msg->len);
    }

    if (msg->dir == BSP_IIC_DIR_TX) {
        return HAL_I2C_Master_Transmit_DMA(hi2c, dev_addr,
                                           msg->data, msg->len);
    }
    return HAL_I2C_Master_Receive_DMA(hi2c, dev_addr, msg->data, msg->len);
}

/**
 * @brief 超时后中止传输并复位外设
 * @param bus 总线对象
 * @retval 无
 * @note 仅任务上下文调用，且调用者已持锁。DeInit 会停掉 DMA 并关闭
 *       传输相关中断，Init 重新按 CubeMX 配置装回外设。从机把 SDA
 *       拉死时本函数救不回来，需要在 MspDeInit 里补 9 个时钟脉冲。
 */
static void BSP_IIC_Abort(bsp_iic_t *bus)
{
    I2C_HandleTypeDef *hi2c = (I2C_HandleTypeDef *)bus->hal;

    (void)HAL_I2C_DeInit(hi2c);
    (void)HAL_I2C_Init(hi2c);
}

/**
 * @brief 中断收尾：记录结果并唤醒等待者
 * @param bus    总线对象
 * @param result 结果码
 * @retval 无
 * @note 只由 HAL 完成/错误回调调用。result 先写、通知后发，任务看到
 *       通知时 result 一定已经落盘。waiter 为 NULL 说明没人等（已超时
 *       放弃），此时只记录结果，不再通知任何人。
 */
static void BSP_IIC_CompleteFromISR(bsp_iic_t *bus, bsp_iic_status_t result)
{
    TaskHandle_t waiter = bus->waiter;
    BaseType_t   woken = pdFALSE;

    bus->result = result;

    if (waiter != NULL) {
        vTaskNotifyGiveFromISR(waiter, &woken);
    }

    portYIELD_FROM_ISR(woken);
}

/*==============================================================================
 * 接口实现
 *============================================================================*/
/**
 * @brief 初始化 IIC 总线驱动：轮询总线表，逐路登记
 * @retval BSP_IIC_OK: 初始化成功（已初始化的总线会被跳过）
 * @retval BSP_IIC_ERR_STATE: 某一路缺 HAL 句柄、互斥量未创建，或外设
 *         尚未回到 READY
 * @note 互斥量由 CubeMX 在 freertos.c 中创建、在实例定义处绑定，所以必须
 *       等 MX_FREERTOS_Init() 建完互斥量再调用（USER CODE BEGIN
 *       RTOS_MUTEX 内）；其余条件是 MX_I2Cx_Init() 已经执行过。本函数
 *       不能在中断里调用。新增一路 IIC 只需在 s_buses[] 追加实例，
 *       本函数不用改。
 */
bsp_iic_status_t BSP_IIC_Init(void)
{
    size_t i;

    /* ---------- 1. 逐路登记：句柄、互斥量、外设状态 ---------- */
    for (i = 0U; i < sizeof(s_buses) / sizeof(s_buses[0]); i++) {
        bsp_iic_t *bus = s_buses[i];

        if (bus->initialized != 0U) {
            continue;
        }
        if (bus->hal == NULL || bus->lock == NULL || *bus->lock == NULL) {
            return BSP_IIC_ERR_STATE;
        }
        if (HAL_I2C_GetState((I2C_HandleTypeDef *)bus->hal) !=
            HAL_I2C_STATE_READY) {
            return BSP_IIC_ERR_STATE;
        }

        /* 清零上一轮可能残留的运行时状态，再标记本路可用 */
        bus->waiter = NULL;
        bus->result = BSP_IIC_OK;
        bus->initialized = 1U;
    }
    return BSP_IIC_OK;
}

/**
 * @brief 提交一次 IIC 传输并阻塞等待完成
 * @param bus 总线句柄，不能为 NULL
 * @param msg 传输命令，不能为 NULL；data 在函数返回前必须保持有效
 * @retval BSP_IIC_OK: 传输成功
 * @retval BSP_IIC_ERR_PARAM: 地址、方向、模式、长度或缓冲区非法
 * @retval BSP_IIC_ERR_STATE: 驱动未初始化
 * @retval BSP_IIC_ERR_TIMEOUT: 排队等锁超时，或传输超时
 * @retval BSP_IIC_ERR_NACK: 从机 NACK
 * @retval BSP_IIC_ERR_BUS: 总线错误
 * @retval BSP_IIC_ERR_HAL: HAL 启动失败或其他 HAL 错误
 * @note 多任务可并发调用，按互斥量排队，先抢到锁的先发送。timeout_ms
 *       同时用于排队和传输，两者共用一份预算。本函数不能在中断中
 *       调用，也不能被同一任务重入。超时后外设会被 DeInit/Init 复位，
 *       总线随即交给下一个排队的任务。
 */
bsp_iic_status_t BSP_IIC_Transfer(bsp_iic_t *bus, const bsp_iic_msg_t *msg)
{
    TickType_t timeout;
    uint32_t   timeout_ms;
    bsp_iic_status_t status;

    if (bus == NULL || msg == NULL) {
        return BSP_IIC_ERR_PARAM;
    }
    if (bus->initialized == 0U) {
        return BSP_IIC_ERR_STATE;
    }
    if (msg->data == NULL || msg->len == 0U) {
        return BSP_IIC_ERR_PARAM;
    }
    if (msg->dev_addr > 0x7FU) {
        return BSP_IIC_ERR_PARAM;
    }
    if (msg->dir > (uint8_t)BSP_IIC_DIR_RX ||
        msg->mode > (uint8_t)BSP_IIC_MODE_MEM) {
        return BSP_IIC_ERR_PARAM;
    }
    if (msg->mode == BSP_IIC_MODE_MEM &&
        msg->mem_addr_size != 1U && msg->mem_addr_size != 2U) {
        return BSP_IIC_ERR_PARAM;
    }

    timeout_ms = (msg->timeout_ms != 0U) ?
                 msg->timeout_ms : BSP_IIC_DEFAULT_TIMEOUT_MS;
    timeout = pdMS_TO_TICKS(timeout_ms);

    /* ---------- 1. 抢总线，抢不到说明排队已经超时 ---------- */
    if (osMutexAcquire(*bus->lock, timeout) != osOK) {
        return BSP_IIC_ERR_TIMEOUT;
    }

    /* ---------- 2. 清掉上次残留的通知，登记等待者 ---------- */
    (void)ulTaskNotifyTake(pdTRUE, 0U);
    bus->result = BSP_IIC_ERR_HAL;
    bus->waiter = xTaskGetCurrentTaskHandle();

    /* ---------- 3. 启动 DMA，这个调用立刻就返回 ---------- */
    if (BSP_IIC_StartHal(bus, msg) != HAL_OK) {
        bus->waiter = NULL;
        BSP_IIC_Abort(bus); /* 启动失败多半是外设卡住，顺手复位 */
        (void)osMutexRelease(*bus->lock);
        return BSP_IIC_ERR_HAL;
    }

    /* ---------- 4. 睡到完成中断唤醒，或者超时 ---------- */
    if (ulTaskNotifyTake(pdTRUE, timeout) == 0U) {
        bus->waiter = NULL;
        BSP_IIC_Abort(bus);
        status = BSP_IIC_ERR_TIMEOUT;
    } else {
        status = bus->result;
    }

    bus->waiter = NULL;
    (void)osMutexRelease(*bus->lock);
    return status;
}

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
                               const uint8_t *data, uint16_t len)
{
    bsp_iic_msg_t msg;

    msg.dev_addr = dev_addr;
    msg.mem_addr = 0U;
    msg.mem_addr_size = 1U;
    msg.len = len;
    msg.dir = (uint8_t)BSP_IIC_DIR_TX;
    msg.mode = (uint8_t)BSP_IIC_MODE_RAW;
    msg.data = (uint8_t *)data;
    msg.timeout_ms = 0U;

    return BSP_IIC_Transfer(bus, &msg);
}

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
                              uint8_t *data, uint16_t len)
{
    bsp_iic_msg_t msg;

    msg.dev_addr = dev_addr;
    msg.mem_addr = 0U;
    msg.mem_addr_size = 1U;
    msg.len = len;
    msg.dir = (uint8_t)BSP_IIC_DIR_RX;
    msg.mode = (uint8_t)BSP_IIC_MODE_RAW;
    msg.data = data;
    msg.timeout_ms = 0U;

    return BSP_IIC_Transfer(bus, &msg);
}

/**
 * @brief 向从机的 8 bit 寄存器子地址写数据（MEM 模式）
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
                                  uint16_t len)
{
    bsp_iic_msg_t msg;

    msg.dev_addr = dev_addr;
    msg.mem_addr = (uint16_t)reg;
    msg.mem_addr_size = 1U;
    msg.len = len;
    msg.dir = (uint8_t)BSP_IIC_DIR_TX;
    msg.mode = (uint8_t)BSP_IIC_MODE_MEM;
    msg.data = (uint8_t *)data;
    msg.timeout_ms = 0U;

    return BSP_IIC_Transfer(bus, &msg);
}

/**
 * @brief 从从机的 8 bit 寄存器子地址读数据（MEM 模式）
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
                                 uint16_t len)
{
    bsp_iic_msg_t msg;

    msg.dev_addr = dev_addr;
    msg.mem_addr = (uint16_t)reg;
    msg.mem_addr_size = 1U;
    msg.len = len;
    msg.dir = (uint8_t)BSP_IIC_DIR_RX;
    msg.mode = (uint8_t)BSP_IIC_MODE_MEM;
    msg.data = data;
    msg.timeout_ms = 0U;

    return BSP_IIC_Transfer(bus, &msg);
}

/*==============================================================================
 * HAL 回调覆盖
 *
 * HAL 只为 Master_*_DMA 调 Master*Cplt，只为 Mem_*_DMA 调 Mem*Cplt
 * （见 stm32f1xx_hal_i2c.c 中 I2C_MasterTransmitCplt / I2C_MasterReceiveCplt
 *  的 MEM_ADD 分支），两组不能互相替代：只实现"发送/接收完成"会让
 *  BSP_IIC_WriteReg/ReadReg 永远等不到完成。
 * 这里覆盖的是 HAL 的 __weak 完成回调，不是 I2C1_EV_IRQHandler 本身；
 * IRQ 向量仍由 Core/Src/stm32f1xx_it.c 的 HAL_I2C_EV_IRQHandler() 分发。
 *============================================================================*/
/**
 * @brief 主机发送完成回调
 * @param hi2c HAL IIC 句柄
 * @note 强定义覆盖 HAL 的 __weak 版本；RAW 发送路径的完成点。
 */
void HAL_I2C_MasterTxCpltCallback(I2C_HandleTypeDef *hi2c)
{
    bsp_iic_t *bus = BSP_IIC_FindBus(hi2c);

    if (bus != NULL) {
        BSP_IIC_CompleteFromISR(bus, BSP_IIC_OK);
    }
}

/**
 * @brief 主机接收完成回调
 * @param hi2c HAL IIC 句柄
 * @note 强定义覆盖 HAL 的 __weak 版本；RAW 接收路径的完成点。
 */
void HAL_I2C_MasterRxCpltCallback(I2C_HandleTypeDef *hi2c)
{
    bsp_iic_t *bus = BSP_IIC_FindBus(hi2c);

    if (bus != NULL) {
        BSP_IIC_CompleteFromISR(bus, BSP_IIC_OK);
    }
}

/**
 * @brief 寄存器写完成回调
 * @param hi2c HAL IIC 句柄
 * @note 强定义覆盖 HAL 的 __weak 版本；MEM 写路径的完成点。
 */
void HAL_I2C_MemTxCpltCallback(I2C_HandleTypeDef *hi2c)
{
    bsp_iic_t *bus = BSP_IIC_FindBus(hi2c);

    if (bus != NULL) {
        BSP_IIC_CompleteFromISR(bus, BSP_IIC_OK);
    }
}

/**
 * @brief 寄存器读完成回调
 * @param hi2c HAL IIC 句柄
 * @note 强定义覆盖 HAL 的 __weak 版本；MEM 读路径的完成点。
 */
void HAL_I2C_MemRxCpltCallback(I2C_HandleTypeDef *hi2c)
{
    bsp_iic_t *bus = BSP_IIC_FindBus(hi2c);

    if (bus != NULL) {
        BSP_IIC_CompleteFromISR(bus, BSP_IIC_OK);
    }
}

/**
 * @brief 错误回调：把 HAL ErrorCode 翻译成 BSP 返回码
 * @param hi2c HAL IIC 句柄
 * @note AF -> NACK；BERR/ARLO/OVR -> BUS；TIMEOUT -> TIMEOUT；其余 HAL。
 */
void HAL_I2C_ErrorCallback(I2C_HandleTypeDef *hi2c)
{
    bsp_iic_t *bus = BSP_IIC_FindBus(hi2c);
    bsp_iic_status_t result;

    if (bus == NULL) {
        return;
    }

    if ((hi2c->ErrorCode & HAL_I2C_ERROR_AF) != 0U) {
        result = BSP_IIC_ERR_NACK;
    } else if ((hi2c->ErrorCode &
                (HAL_I2C_ERROR_BERR | HAL_I2C_ERROR_ARLO |
                 HAL_I2C_ERROR_OVR)) != 0U) {
        result = BSP_IIC_ERR_BUS;
    } else if ((hi2c->ErrorCode & HAL_I2C_ERROR_TIMEOUT) != 0U) {
        result = BSP_IIC_ERR_TIMEOUT;
    } else {
        result = BSP_IIC_ERR_HAL;
    }

    BSP_IIC_CompleteFromISR(bus, result);
}
