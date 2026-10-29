/**
 * @file bsp_spi.c
 * @brief SPI 硬件驱动层：互斥量排队 + 完成中断唤醒，总线/设备两级句柄
 * @author 李嘉图
 * @date 2026-10-29
 *
 * @note 设计模型：
 *       1. 每个 bsp_spi_t 实例绑一把互斥量，任务先抢锁再启动传输；
 *          抢不到就在锁上排队，排队深度等于任务数，没有固定上限；
 *       2. 外设配了 DMA 就用 HAL 的 DMA 接口启动，启动调用一进去就返回，
 *          完成/错误中断只做两件事：记录结果、唤醒占着总线的任务；
 *       3. 没配 DMA 的外设自动退回 HAL 阻塞接口，语义与返回值保持一致，
 *          所以本模块在 SPI1 / SPI2 上都能直接复用；
 *       4. 对外只有两个传输入口：BSP_SPI_Transfer() 不管片选，
 *          BSP_SPI_Dev_Transfer() 自动拉放片选；方向由 tx / rx 是否为
 *          NULL 推断（只发 / 只收 / 全双工），不传方向参数、不传结构体；
 *       5. 设备级接口在传输前后自动拉放片选，一条总线挂几个设备只差几个
 *          句柄实例，不改任何函数。
 *
 *       OS 对象归属：互斥量 SPI_LOCK 定义在 frame.ioc，由 CubeMX 生成到
 *       freertos.c；本文件只 extern 使用，不创建任务、队列或同步对象。
 *       BSP_SPI_Init() 必须放在 freertos.c 的 USER CODE BEGIN RTOS_MUTEX，
 *       那里 SPI_LOCKHandle 已经建好。
 *
 *       链接注意：HAL 回调是强符号，必须保证 bsp_spi.c.o 被拉进最终
 *       镜像；freertos.c 的 BSP_SPI_Init() 调用会完成这件事。
 */

/*==============================================================================
 * 头文件包含
 *============================================================================*/
// 固定包含
#include <stddef.h>
#include <stdint.h>
#include "bsp_spi.h"
// 功能包含
#include "spi.h"
#include "bsp_gpio.h"
#include "FreeRTOS.h"
#include "task.h"
#include "cmsis_os.h"

/*==============================================================================
 * 外部变量声明
 *============================================================================*/
/** @brief CubeMX 生成的互斥量句柄（frame.ioc 里配置的 SPI_LOCK） */
extern osMutexId_t SPI_LOCKHandle;

/*==============================================================================
 * 宏定义与常量
 *============================================================================*/
/** @brief 单笔传输默认超时（ms），排队与总线传输共用这一份预算 */
#define BSP_SPI_DEFAULT_TIMEOUT_MS  50U

/** @brief 方向能力位：外设配置允许的传输方向
 *  @note 只有 TX / RX 两个基本位，TXRX 是两者的组合，不是第三个独立位。
 *        检查统一用 (caps & need) == need，组合位与单个位走同一套判断。 */
#define BSP_SPI_CAP_TX    0x01U /* 可只发 */
#define BSP_SPI_CAP_RX    0x02U /* 可只收 */
#define BSP_SPI_CAP_TXRX  (BSP_SPI_CAP_TX | BSP_SPI_CAP_RX) /* 可全双工收发 */

/** @brief DMA 能力位：外设上实际接了哪几路 DMA
 *  @note TX / RX 是两个独立硬件事实（各有各的通道），全双工要求两者同时
 *        成立，所以 TXRX 只能是组合表达式，绝不能另占一位。 */
#define BSP_SPI_DMA_TX    0x01U /* 发送 DMA 已配置 */
#define BSP_SPI_DMA_RX    0x02U /* 接收 DMA 已配置 */
#define BSP_SPI_DMA_TXRX  (BSP_SPI_DMA_TX | BSP_SPI_DMA_RX) /* 收发两路都在 */

/*==============================================================================
 * 类型定义
 *============================================================================*/
/**
 * @brief 传输方向（内部类型）
 * @note 不进公共头文件：调用者靠 tx / rx 是否为 NULL 隐式表达方向，
 *       由 BSP_SPI_Transfer() 推断后填进来，不需要也不应该自己填。
 */
typedef enum {
    BSP_SPI_DIR_TX   = 0, /* 只发：发命令、写寄存器、刷屏 */
    BSP_SPI_DIR_RX   = 1, /* 只收：读传感器、读状态 */
    BSP_SPI_DIR_TXRX = 2, /* 全双工同时收发 */
} bsp_spi_dir_t;

/**
 * @brief SPI 总线对象
 * @note  hal 是 void *，避免公共头文件引入 HAL 类型；lock 存的是互斥量
 *        句柄的地址——句柄值要等 freertos.c 运行时才产生，存地址就能在
 *        实例定义处一次配好，初始化时只做校验。
 *        caps / dma_mask 是 Init 阶段从 HAL 配置里读出来的能力快照，
 *        传输路径只查位，不再回头读外设寄存器。
 */
struct bsp_spi {
    void             *hal;        /* HAL SPI 句柄（SPI_HandleTypeDef *） */
    osMutexId_t      *lock;       /* 指向 freertos.c 生成的互斥量句柄 */
    TaskHandle_t volatile waiter; /* 正在等完成的任务，空闲为 NULL */
    volatile bsp_spi_status_t result;  /* 本次传输结果，由中断写入 */
    volatile uint8_t  initialized;     /* 1: 已完成初始化 */
    uint8_t           caps;            /* 方向能力位，见 BSP_SPI_CAP_* */
    uint8_t           dma_mask;        /* DMA 能力位，见 BSP_SPI_DMA_* */
};

/**
 * @brief SPI 设备对象
 * @note 只存"总线 + 片选 + 极性"三样，不存 SCK/MOSI 引脚——那些属于总线。
 *       设备句柄是 const 的，编译期进 .rodata，不占 RAM。
 */
struct bsp_spi_dev {
    bsp_spi_t        *bus;             /* 所属总线，不能为 NULL */
    const bsp_gpio_t *cs;              /* 片选引脚句柄，NULL 表示不用片选 */
    uint8_t           cs_active_level; /* 1: 高电平有效；0: 低电平有效 */
};

/**
 * @brief 一次传输的内部描述
 * @note 只在本文件内使用，不进公共头文件。dir 由两个缓冲区指针推出来，
 *       调用者不用填：tx 有效即发、rx 有效即收，两个都有效是全双工。
 */
typedef struct {
    const uint8_t *tx;  /* 发送缓冲区，NULL 表示不发 */
    uint8_t       *rx;  /* 接收缓冲区，NULL 表示不收 */
    uint16_t       len; /* 传输字节数，不能为 0 */
    uint8_t        dir; /* 方向，取 bsp_spi_dir_t */
} bsp_spi_msg_t;

/*==============================================================================
 * 内部变量
 *============================================================================*/
/** @brief SPI2 总线对象；hal/lock 都指向 CubeMX 生成的句柄 */
bsp_spi_t bsp_spi2 = {
    .hal         = &hspi2,
    .lock        = &SPI_LOCKHandle,
    .waiter      = NULL,
    .result      = 0,
    .initialized = 0U,
    .caps        = 0U,
    .dma_mask    = 0U,
};

/** @brief 挂在 SPI2 上的 LCD：片选 PA8，低电平有效（DC/RES/BLK 由设备层管） */
const bsp_spi_dev_t bsp_spi2_dev_lcd = {
    .bus             = &bsp_spi2,
    .cs              = &bsp_gpio_pa8,
    .cs_active_level = 0U,
};

/** @brief 驱动管理的总线表；增加 SPI1 时只在这里追加实例，
 *         初始化和中断反查都会自动轮询到它 */
static bsp_spi_t *const s_buses[] = {
    &bsp_spi2,
};

/*==============================================================================
 * 内部函数
 *============================================================================*/
/**
 * @brief 按 HAL 句柄查找对应的 BSP 总线对象
 * @param hspi HAL SPI 句柄
 * @retval 非 NULL: 对应的总线对象
 * @retval NULL: 不是本驱动管理的句柄
 */
static bsp_spi_t *BSP_SPI_FindBus(SPI_HandleTypeDef *hspi)
{
    size_t i;

    if (hspi == NULL) {
        return NULL;
    }

    for (i = 0U; i < sizeof(s_buses) / sizeof(s_buses[0]); i++) {
        if (s_buses[i]->hal == (void *)hspi) {
            return s_buses[i];
        }
    }
    return NULL;
}

/**
 * @brief 由 CubeMX 配置的方向推出本总线支持哪些传输方向
 * @param hspi HAL SPI 句柄
 * @retval 方向能力位组合，见 BSP_SPI_CAP_*
 * @note 2LINES 全双工三种都能做；1LINE 只有 MOSI 一根线，只能发；
 *       2LINES_RXONLY 只开了接收通道，只能收。
 */
static uint8_t BSP_SPI_CapsOf(const SPI_HandleTypeDef *hspi)
{
    if (hspi->Init.Direction == SPI_DIRECTION_1LINE) {
        return BSP_SPI_CAP_TX;
    }
    if (hspi->Init.Direction == SPI_DIRECTION_2LINES_RXONLY) {
        return BSP_SPI_CAP_RX;
    }
    return (uint8_t)BSP_SPI_CAP_TXRX; /* 2LINES：TX、RX、TXRX 三种方向都能做 */
}

/**
 * @brief 把传输方向翻译成需要的能力位
 * @param dir 方向，取 bsp_spi_dir_t
 * @retval 对应的 BSP_SPI_CAP_* 位
 */
static uint8_t BSP_SPI_CapOfDir(uint8_t dir)
{
    if (dir == (uint8_t)BSP_SPI_DIR_TX) {
        return BSP_SPI_CAP_TX;
    }
    if (dir == (uint8_t)BSP_SPI_DIR_RX) {
        return BSP_SPI_CAP_RX;
    }
    return (uint8_t)BSP_SPI_CAP_TXRX;
}

/**
 * @brief 把传输方向翻译成需要的 DMA 通道位
 * @param dir 方向，取 bsp_spi_dir_t
 * @retval 对应的 BSP_SPI_DMA_* 位组合
 * @note 全双工要收发两路 DMA 都在，缺一路就整体退回阻塞模式。
 */
static uint8_t BSP_SPI_DmaOfDir(uint8_t dir)
{
    if (dir == (uint8_t)BSP_SPI_DIR_TX) {
        return BSP_SPI_DMA_TX;
    }
    if (dir == (uint8_t)BSP_SPI_DIR_RX) {
        return BSP_SPI_DMA_RX;
    }
    return (uint8_t)BSP_SPI_DMA_TXRX;
}

/**
 * @brief 按命令方向启动一次 HAL DMA 传输
 * @param bus 总线对象
 * @param msg 传输命令
 * @retval HAL_OK: 启动成功
 * @retval 其他: HAL 启动失败
 * @note 只由传输函数在任务上下文、已持锁时调用；启动即返回，完成点
 *       在对应的 HAL_SPI_*CpltCallback 里。
 */
static HAL_StatusTypeDef BSP_SPI_StartDma(bsp_spi_t *bus,
                                          const bsp_spi_msg_t *msg)
{
    SPI_HandleTypeDef *hspi = (SPI_HandleTypeDef *)bus->hal;

    if (msg->dir == (uint8_t)BSP_SPI_DIR_TX) {
        return HAL_SPI_Transmit_DMA(hspi, msg->tx, msg->len);
    }
    if (msg->dir == (uint8_t)BSP_SPI_DIR_RX) {
        return HAL_SPI_Receive_DMA(hspi, msg->rx, msg->len);
    }
    return HAL_SPI_TransmitReceive_DMA(hspi, msg->tx, msg->rx, msg->len);
}

/**
 * @brief 没有 DMA 时用 HAL 阻塞接口搬完
 * @param bus        总线对象
 * @param msg        传输命令
 * @param timeout_ms 本次传输的超时预算
 * @retval BSP_SPI_OK: 传输成功
 * @retval BSP_SPI_ERR_TIMEOUT: 传输超时
 * @retval BSP_SPI_ERR_HAL: HAL 返回错误
 * @note 传输期间调用者已持锁，这里只做"启动 + 等完成"，不碰片选。
 *       超时预算要覆盖最慢的一次整帧（刷屏一次几十 KB）。
 */
static bsp_spi_status_t BSP_SPI_RunBlocking(bsp_spi_t *bus,
                                            const bsp_spi_msg_t *msg,
                                            uint32_t timeout_ms)
{
    SPI_HandleTypeDef *hspi = (SPI_HandleTypeDef *)bus->hal;
    HAL_StatusTypeDef  st;

    if (msg->dir == (uint8_t)BSP_SPI_DIR_TX) {
        st = HAL_SPI_Transmit(hspi, msg->tx, msg->len, timeout_ms);
    } else if (msg->dir == (uint8_t)BSP_SPI_DIR_RX) {
        st = HAL_SPI_Receive(hspi, msg->rx, msg->len, timeout_ms);
    } else {
        st = HAL_SPI_TransmitReceive(hspi, msg->tx, msg->rx, msg->len,
                                     timeout_ms);
    }

    if (st == HAL_OK) {
        return BSP_SPI_OK;
    }
    if (st == HAL_TIMEOUT) {
        return BSP_SPI_ERR_TIMEOUT;
    }
    return BSP_SPI_ERR_HAL;
}

/**
 * @brief 超时后中止传输并让外设回到 READY
 * @param bus 总线对象
 * @retval 无
 * @note 仅任务上下文调用，且调用者已持锁。HAL_SPI_Abort 会停掉 DMA 并
 *       把状态机复位；个别情况下 Abort 自身失败（DMA 已跑飞），此时用
 *       DeInit/Init 硬复位一次，保证总线能交给下一个排队的任务。
 */
static void BSP_SPI_Abort(bsp_spi_t *bus)
{
    SPI_HandleTypeDef *hspi = (SPI_HandleTypeDef *)bus->hal;

    if (HAL_SPI_Abort(hspi) != HAL_OK) {
        (void)HAL_SPI_DeInit(hspi);
        (void)HAL_SPI_Init(hspi);
    }
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
static void BSP_SPI_CompleteFromISR(bsp_spi_t *bus, bsp_spi_status_t result)
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
 * @brief 初始化 SPI 驱动：轮询总线表，登记句柄与方向能力
 * @retval BSP_SPI_OK: 初始化成功（已初始化的总线会被跳过）
 * @retval BSP_SPI_ERR_STATE: 某一路缺 HAL 句柄、互斥量未创建、外设没回到
 *         READY、Mode 不是 Master，或数据宽度不是 8 bit
 * @note 互斥量由 CubeMX 在 freertos.c 中创建、在实例定义处绑定，所以必须
 *       等 MX_FREERTOS_Init() 建完互斥量再调用（USER CODE BEGIN
 *       RTOS_MUTEX 内）；其余条件是 MX_SPIx_Init() 已经执行过。本函数
 *       不能在中断里调用。新增一路 SPI 只需在 s_buses[] 追加实例，
 *       本函数不用改。
 */
bsp_spi_status_t BSP_SPI_Init(void)
{
    size_t i;

    /* ---------- 1. 逐路登记：句柄、互斥量、外设状态 ---------- */
    for (i = 0U; i < sizeof(s_buses) / sizeof(s_buses[0]); i++) {
        bsp_spi_t        *bus = s_buses[i];
        SPI_HandleTypeDef *hspi;

        if (bus->initialized != 0U) {
            continue;
        }
        if (bus->hal == NULL || bus->lock == NULL || *bus->lock == NULL) {
            return BSP_SPI_ERR_STATE;
        }

        hspi = (SPI_HandleTypeDef *)bus->hal;
        if (HAL_SPI_GetState(hspi) != HAL_SPI_STATE_READY) {
            return BSP_SPI_ERR_STATE;
        }
        /* 本层只服务主机模式：从机是"等主机给时钟"，语义与超时都不同 */
        if (hspi->Init.Mode != SPI_MODE_MASTER) {
            return BSP_SPI_ERR_STATE;
        }
        /* 本层只按字节数收发，16 bit 宽度会让 len 的语义变成半字数 */
        if (hspi->Init.DataSize != SPI_DATASIZE_8BIT) {
            return BSP_SPI_ERR_STATE;
        }

        /* ---------- 2. 记下方向能力与 DMA 能力 ---------- */
        bus->caps = BSP_SPI_CapsOf(hspi);
        bus->dma_mask = 0U;
        if (hspi->hdmatx != NULL) {
            bus->dma_mask |= BSP_SPI_DMA_TX;
        }
        if (hspi->hdmarx != NULL) {
            bus->dma_mask |= BSP_SPI_DMA_RX;
        }

        /* 清零上一轮可能残留的运行时状态，再标记本路可用 */
        bus->waiter = NULL;
        bus->result = BSP_SPI_OK;
        bus->initialized = 1U;
    }
    return BSP_SPI_OK;
}

/**
 * @brief 在指定总线上提交一次传输并阻塞等待完成（不管片选）
 * @param bus        总线句柄，不能为 NULL
 * @param tx         发送缓冲区，NULL 表示本次不发
 * @param rx         接收缓冲区，NULL 表示本次不收
 * @param len        传输字节数，不能为 0
 * @param timeout_ms 超时预算，0 表示用默认 50 ms
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
 *       本函数不管片选：单设备常选、或需要自己控制片选保持时用它；多设备
 *       常规收发用 BSP_SPI_Dev_Transfer()。多任务可并发调用，按互斥量
 *       排队。不能在中断中调用，也不能被同一任务重入。timeout_ms 同时
 *       用于排队和传输，两者共用一份预算；超时后外设会被中止复位，总线
 *       随即交给下一个排队的任务。
 */
bsp_spi_status_t BSP_SPI_Transfer(bsp_spi_t *bus, const uint8_t *tx,
                                  uint8_t *rx, uint16_t len,
                                  uint32_t timeout_ms)
{
    TickType_t       timeout;
    bsp_spi_msg_t    msg;
    uint8_t          need_cap;
    uint8_t          need_dma;
    bsp_spi_status_t status;

    /* ---------- 1. 参数检查，方向由缓冲区指针推出来 ---------- */
    if (bus == NULL) {
        return BSP_SPI_ERR_PARAM;
    }
    if (bus->initialized == 0U) {
        return BSP_SPI_ERR_STATE;
    }
    if ((len == 0U) || ((tx == NULL) && (rx == NULL))) {
        return BSP_SPI_ERR_PARAM;
    }

    msg.tx = tx;
    msg.rx = rx;
    msg.len = len;
    if ((tx != NULL) && (rx != NULL)) {
        msg.dir = (uint8_t)BSP_SPI_DIR_TXRX;
    } else if (tx != NULL) {
        msg.dir = (uint8_t)BSP_SPI_DIR_TX;
    } else {
        msg.dir = (uint8_t)BSP_SPI_DIR_RX;
    }

    /* 组合位按"全含"判断：TXRX 必须 TX、RX 两位都在 */
    need_cap = BSP_SPI_CapOfDir(msg.dir);
    if ((bus->caps & need_cap) != need_cap) {
        return BSP_SPI_ERR_MODE;
    }

    if (timeout_ms == 0U) {
        timeout_ms = BSP_SPI_DEFAULT_TIMEOUT_MS;
    }
    timeout = pdMS_TO_TICKS(timeout_ms);
    need_dma = BSP_SPI_DmaOfDir(msg.dir);

    /* ---------- 2. 抢总线，抢不到说明排队已经超时 ---------- */
    if (osMutexAcquire(*bus->lock, timeout) != osOK) {
        return BSP_SPI_ERR_TIMEOUT;
    }

    /* ---------- 3. 有 DMA：启动后睡等完成中断 ---------- */
    if ((bus->dma_mask & need_dma) == need_dma) {
        /* 清掉上次残留的通知，登记等待者 */
        (void)ulTaskNotifyTake(pdTRUE, 0U);
        bus->result = BSP_SPI_ERR_HAL;
        bus->waiter = xTaskGetCurrentTaskHandle();

        if (BSP_SPI_StartDma(bus, &msg) != HAL_OK) {
            bus->waiter = NULL;
            BSP_SPI_Abort(bus); /* 启动失败多半是外设卡住，顺手复位 */
            (void)osMutexRelease(*bus->lock);
            return BSP_SPI_ERR_HAL;
        }

        if (ulTaskNotifyTake(pdTRUE, timeout) == 0U) {
            bus->waiter = NULL;
            BSP_SPI_Abort(bus);
            status = BSP_SPI_ERR_TIMEOUT;
        } else {
            status = bus->result;
        }
    } else {
        /* ---------- 4. 没有 DMA：直接阻塞搬完 ---------- */
        status = BSP_SPI_RunBlocking(bus, &msg, timeout_ms);
        if (status != BSP_SPI_OK) {
            BSP_SPI_Abort(bus);
        }
    }

    bus->waiter = NULL;
    (void)osMutexRelease(*bus->lock);
    return status;
}

/**
 * @brief 拉有效片选
 * @param dev 设备句柄，不能为 NULL
 * @note 电平按句柄里的 cs_active_level 换算，调用者不用关心极性。
 *       常规单次收发不需要直接调用，BSP_SPI_Dev_Transfer() 会自己做；
 *       需要一帧内分多段发、片选全程保持低时，用它自己成对包住若干次
 *       BSP_SPI_Transfer()。句柄没绑片选（cs 为 NULL）时本函数是空操作。
 */
void BSP_SPI_Dev_Select(const bsp_spi_dev_t *dev)
{
    if ((dev == NULL) || (dev->cs == NULL)) {
        return;
    }
    BSP_GPIO_Write(dev->cs, dev->cs_active_level);
}

/**
 * @brief 放无效片选
 * @param dev 设备句柄，不能为 NULL
 * @note 与 BSP_SPI_Dev_Select() 配对，用于一帧分多段发、片选全程保持低
 *       的场景。句柄没绑片选时本函数是空操作。
 */
void BSP_SPI_Dev_Deselect(const bsp_spi_dev_t *dev)
{
    if ((dev == NULL) || (dev->cs == NULL)) {
        return;
    }
    BSP_GPIO_Write(dev->cs, (dev->cs_active_level == 0U) ? 1U : 0U);
}

/**
 * @brief 在指定设备上提交一次传输，自动拉放片选
 * @param dev        设备句柄，不能为 NULL
 * @param tx         发送缓冲区，NULL 表示本次不发
 * @param rx         接收缓冲区，NULL 表示本次不收
 * @param len        传输字节数，不能为 0
 * @param timeout_ms 超时预算，0 表示用默认 50 ms
 * @retval BSP_SPI_OK: 传输成功
 * @retval BSP_SPI_ERR_PARAM: 句柄或长度非法，或两个缓冲区都为 NULL
 * @retval BSP_SPI_ERR_STATE: 驱动未初始化
 * @retval BSP_SPI_ERR_TIMEOUT: 排队或传输超时
 * @retval BSP_SPI_ERR_MODE: 外设当前方向不支持本次传输
 * @retval BSP_SPI_ERR_BUS: 总线错误
 * @retval BSP_SPI_ERR_HAL: HAL 错误
 * @note 方向推断规则与 BSP_SPI_Transfer() 完全一致：只发、只收、全双工
 *       都靠 tx / rx 是否为 NULL 区分。片选在传输前拉有效、返回前放
 *       无效，成功失败都放。需要片选跨多次传输保持低，就别用本函数，
 *       改用 BSP_SPI_Dev_Select() + BSP_SPI_Transfer() 自己成对包住。
 */
bsp_spi_status_t BSP_SPI_Dev_Transfer(const bsp_spi_dev_t *dev,
                                      const uint8_t *tx, uint8_t *rx,
                                      uint16_t len, uint32_t timeout_ms)
{
    bsp_spi_status_t status;

    if ((dev == NULL) || (dev->bus == NULL)) {
        return BSP_SPI_ERR_PARAM;
    }

    /* ---------- 1. 拉片选，把总线交给这台设备 ---------- */
    BSP_SPI_Dev_Select(dev);

    /* ---------- 2. 搬字节，成功失败都要把片选还回去 ---------- */
    status = BSP_SPI_Transfer(dev->bus, tx, rx, len, timeout_ms);
    BSP_SPI_Dev_Deselect(dev);
    return status;
}

/*==============================================================================
 * HAL 回调覆盖
 *
 * HAL 只为 Transmit_DMA 调 TxCplt、只为 Receive_DMA 调 RxCplt、只为
 * TransmitReceive_DMA 调 TxRxCplt，三个完成点不能互相替代。
 * 这里覆盖的是 HAL 的 __weak 完成回调，不是 SPI2_IRQHandler 本身；
 * IRQ 向量仍由 Core/Src/stm32f1xx_it.c 的 HAL_SPI_IRQHandler() 分发。
 *============================================================================*/
/**
 * @brief 发送完成回调
 * @param hspi HAL SPI 句柄
 * @note 强定义覆盖 HAL 的 __weak 版本；DIR_TX 路径的完成点。
 */
void HAL_SPI_TxCpltCallback(SPI_HandleTypeDef *hspi)
{
    bsp_spi_t *bus = BSP_SPI_FindBus(hspi);

    if (bus != NULL) {
        BSP_SPI_CompleteFromISR(bus, BSP_SPI_OK);
    }
}

/**
 * @brief 接收完成回调
 * @param hspi HAL SPI 句柄
 * @note 强定义覆盖 HAL 的 __weak 版本；DIR_RX 路径的完成点。
 */
void HAL_SPI_RxCpltCallback(SPI_HandleTypeDef *hspi)
{
    bsp_spi_t *bus = BSP_SPI_FindBus(hspi);

    if (bus != NULL) {
        BSP_SPI_CompleteFromISR(bus, BSP_SPI_OK);
    }
}

/**
 * @brief 全双工收发完成回调
 * @param hspi HAL SPI 句柄
 * @note 强定义覆盖 HAL 的 __weak 版本；DIR_TXRX 路径的完成点。
 */
void HAL_SPI_TxRxCpltCallback(SPI_HandleTypeDef *hspi)
{
    bsp_spi_t *bus = BSP_SPI_FindBus(hspi);

    if (bus != NULL) {
        BSP_SPI_CompleteFromISR(bus, BSP_SPI_OK);
    }
}

/**
 * @brief 错误回调：把 HAL ErrorCode 翻译成 BSP 返回码
 * @param hspi HAL SPI 句柄
 * @note MODF（多主机冲突）/ OVR（溢出丢字节）/ CRC 都算总线问题；
 *       DMA 与 FLAG 类错误归到 HAL。主机模式下 MODF 说明 NSS 被外部
 *       拉低（总线上还有第二个主机），复位外设即可恢复。
 */
void HAL_SPI_ErrorCallback(SPI_HandleTypeDef *hspi)
{
    bsp_spi_t       *bus = BSP_SPI_FindBus(hspi);
    bsp_spi_status_t result;

    if (bus == NULL) {
        return;
    }

    if ((hspi->ErrorCode &
         (HAL_SPI_ERROR_MODF | HAL_SPI_ERROR_OVR | HAL_SPI_ERROR_CRC)) != 0U) {
        result = BSP_SPI_ERR_BUS;
    } else {
        result = BSP_SPI_ERR_HAL;
    }

    BSP_SPI_CompleteFromISR(bus, result);
}
