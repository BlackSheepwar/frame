/**
 * @file svc_key_logic.c
 * @brief 服务层实现：消抖、单击/长按判定，事件打包后投进队列
 * @author 李嘉图
 * @date 2026-10-23
 *
 * @note 本层不接触电平：设备层（Dev_Key_Read）已经把它变成了
 *       "按下 / 松开"这种结论，本层只关心这个结论稳不稳、按了多久。
 *
 *       两步走，每一拍都只做这两步：
 *         1. 判稳 —— 读数连续 debounce_ms 不变，才承认它；
 *         2. 计 时 —— 承认之后按按住时长产出 DOWN / CLICK / LONG / REPEAT / LONG_UP。
 *
 *       时间由调用方按毫秒喂进来（扫描任务用 osDelayUntil 固定 10 ms 节拍），
 *       所以阈值改毫秒就行，不需要换算成采样次数。
 *
 *       按键数量与手感参数在下面的"配置表"分区里调：
 *       加一个键 = 表里加一行 + Device 层定义它的句柄。
 *
 *       投递出口只有一个：Svc_Key_Logic_SendEvent()。它投的是 CubeMX 生成的消息队列
 *       KEY_EVT_Q（frame.ioc 的 Queues01：深度 8、元素 key_event_msg_t、静态分配）。
 *       想换投递方式，只改这个函数的函数体，本层其余代码不需要知道事件去了哪。
 */

/*==============================================================================
 * 头文件包含
 *============================================================================*/
// 固定包含
#include <stddef.h>
#include "svc_key_logic.h"
// 功能包含
#include "cmsis_os2.h"
#include "dev_key.h"

/*==============================================================================
 * 外部声明
 *============================================================================*/
/**
 * @brief 事件队列句柄：由 CubeMX 按 frame.ioc 的 Queues01 在 Core/Src/freertos.c 里创建
 * @note 深度 8、元素 key_event_msg_t、静态分配；本文件只负责投递，取出由消费者自己做。
 */
extern osMessageQueueId_t KEY_EVT_QHandle;

/*==============================================================================
 * 内部类型
 *============================================================================*/
/**
 * @brief 一个按键的运行时状态
 * @note reading 是"这一拍读到什么"，pressed 是"确认下来是什么"：
 *       前者一直在变（抖），后者只在前者稳定够久之后才跟着变。
 */
typedef struct {
    uint16_t stable_ms;     /* 当前读数已经稳定了多久（判消抖用） */
    uint16_t hold_ms;       /* 确认按住之后，累计按了多久（判长按用） */
    uint16_t repeat_ms;     /* 距上一次连发过了多久 */
    uint8_t  reading;       /* 这一拍的读数：1 = 按下，0 = 松开 */
    uint8_t  pressed;       /* 确认后的状态：1 = 按住，0 = 松开 */
    uint8_t  long_fired;    /* 本次按住是否已经报过 LONG */
} key_state_t;

/*==============================================================================
 * 配置表
 *============================================================================*/
/**
 * @brief 一个按键的全部可调参数
 * @note 句柄只读，表本身也是 const：整张表进 .rodata，不占 RAM。
 *       键的身份就是句柄本身，表里不再另配一个逻辑编号：
 *       少一个字段，就少一处"编号和句柄对不上"的可能。
 */
typedef struct {
    const dev_key_t *key;           /* 设备句柄，读电平；也是命令表的匹配键 */
    uint16_t         debounce_ms;   /* 该键的消抖窗口 */
    uint16_t         long_ms;       /* 该键的长按阈值 */
    uint16_t         repeat_ms;     /* 该键的连发间隔 */
} svc_key_cfg_t;

/**
 * @brief 板上三个按键：加键 PB11、减键 PB1、灯键 PB10，手感参数一致
 * @note 只在本文件里用，所以是 static：整张表进 .rodata，不占 RAM。
 *       加一个键 = 表里加一行 + Device 层定义它的句柄，本文件其余代码不用动。
 */
static const svc_key_cfg_t s_key_cfg_table[] = {
    { &dev_key_inc, 20u, 200u, 200u },
    { &dev_key_dec, 20u, 200u, 200u },
    { &dev_key_led, 20u, 200u, 200u },
};

/**
 * @brief 表里有多少个键
 * @note 运行时状态数组按这个数分配，加键不需要改实现。
 */
#define SVC_KEY_CFG_COUNT   (sizeof(s_key_cfg_table) / sizeof(*s_key_cfg_table))

/*==============================================================================
 * 静态数据
 *============================================================================*/
/**
 * @brief 每个按键的运行时状态，个数跟配置表一致
 * @note 配置表是 const（进 .rodata），只有这里会变，RAM 占用只跟键数有关。
 */
static key_state_t s_key_state[SVC_KEY_CFG_COUNT];

/**
 * @brief 队列满导致丢弃的次数
 * @note 只用于排查：队列长期满说明消费线程卡住了。
 */
static uint32_t s_key_drop_count;

/*==============================================================================
 * 内部函数
 *============================================================================*/
/**
 * @brief 无符号相加并饱和到 0xFFFF，避免回绕成"还没到"
 * @param base 当前值
 * @param add  增量
 * @retval 饱和后的和
 */
static uint16_t Svc_Key_Logic_Sat_Add(uint16_t base, uint16_t add)
{
    uint32_t sum = (uint32_t)base + (uint32_t)add;

    return (sum > 0xFFFFu) ? 0xFFFFu : (uint16_t)sum;
}

/**
 * @brief 投递一条事件给下游
 * @param key   按键设备句柄，原样带给下游；本层不解释它是谁
 * @param event 事件类型
 * @note 超时传 0：扫描任务绝不在这里阻塞；队列满就丢一条并计数。
 *       换后端（环形缓冲、回调）只改这一个函数体，调用处不用动。
 */
static void Svc_Key_Logic_SendEvent(const dev_key_t *key, key_event_t event)
{
    key_event_msg_t msg;

    msg.key   = key;
    msg.event = event;

    // 这里使用了FreeRTOS的队列，投递事件给下游消费者
    // 如果是裸机可以给本服务增加一个FIFO附属文件，再这里投递事件到FIFO中进行事件管理
    if (osMessageQueuePut(KEY_EVT_QHandle, &msg, 0u, 0u) != osOK) {
        s_key_drop_count++;
    }
}

/**
 * @brief 刚确认按下：报 DOWN，并把长按计时从"已经按住多久"开始
 * @param state 运行时状态
 * @param cfg   该键的配置
 * @note hold_ms 取 stable_ms 是为了把消抖窗口也算进按住时间：
 *       手指真正按下在窗口开头，判定在窗口结尾。
 */
static void Svc_Key_Logic_OnPress(key_state_t *state, const svc_key_cfg_t *cfg)
{
    state->pressed    = 1u;
    state->hold_ms    = state->stable_ms;
    state->repeat_ms  = 0u;
    state->long_fired = 0u;

    Svc_Key_Logic_SendEvent(cfg->key, KEY_EVENT_DOWN);
}

/**
 * @brief 刚确认松开：报 CLICK 或 LONG_UP
 * @param state 运行时状态
 * @param cfg   该键的配置
 * @note 报过 LONG 的松手是"长按结束"，没报过就是"单击"。
 */
static void Svc_Key_Logic_OnRelease(key_state_t *state, const svc_key_cfg_t *cfg)
{
    state->pressed = 0u;

    Svc_Key_Logic_SendEvent(cfg->key,
                            (state->long_fired != 0u) ? KEY_EVENT_LONG_UP : KEY_EVENT_CLICK);
}

/**
 * @brief 按住状态下的计时：先等长按阈值，过了阈值就按间隔连发
 * @param state 运行时状态
 * @param cfg   该键的配置
 * @param elapsed_ms 本次节拍的毫秒数
 */
static void Svc_Key_Logic_OnHold(key_state_t *state, const svc_key_cfg_t *cfg,
                                 uint16_t elapsed_ms)
{
    /* ---------- 1. 还没到长按：攒时长 ---------- */
    if (state->long_fired == 0u) {
        state->hold_ms = Svc_Key_Logic_Sat_Add(state->hold_ms, elapsed_ms);

        if (state->hold_ms >= cfg->long_ms) {
            state->long_fired = 1u;
            state->repeat_ms  = 0u;
            Svc_Key_Logic_SendEvent(cfg->key, KEY_EVENT_LONG);
        }

        return;
    }

    /* ---------- 2. 已经报过长按：按 repeat_ms 一直发 ---------- */
    state->repeat_ms = Svc_Key_Logic_Sat_Add(state->repeat_ms, elapsed_ms);

    if (state->repeat_ms >= cfg->repeat_ms) {
        state->repeat_ms = 0u;
        Svc_Key_Logic_SendEvent(cfg->key, KEY_EVENT_REPEAT);
    }
}

/**
 * @brief 推进一个按键的一拍：判稳 → 状态切换 → 计时
 * @param state 运行时状态
 * @param cfg   该键的配置
 * @param elapsed_ms 本次节拍的毫秒数
 */
static void Svc_Key_Logic_Step(key_state_t *state, const svc_key_cfg_t *cfg,
                               uint16_t elapsed_ms)
{
    uint8_t reading = Dev_Key_Read(cfg->key);

    /* ---------- 1. 读数变了：从这一拍重新计时，先不承认 ---------- */
    if (reading != state->reading) {
        state->reading   = reading;
        state->stable_ms = elapsed_ms;
        return;
    }

    /* ---------- 2. 读数没变：攒够 debounce_ms 才算数 ---------- */
    state->stable_ms = Svc_Key_Logic_Sat_Add(state->stable_ms, elapsed_ms);

    if (state->stable_ms < cfg->debounce_ms) {
        return;
    }

    /* ---------- 3. 稳了而且是个新状态：切换并产事件 ---------- */
    if (reading != state->pressed) {
        if (reading != 0u) {
            Svc_Key_Logic_OnPress(state, cfg);
        } else {
            Svc_Key_Logic_OnRelease(state, cfg);
        }

        return;
    }

    /* ---------- 4. 状态没变：按住就继续计时，松开什么都不用做 ---------- */
    if (state->pressed != 0u) {
        Svc_Key_Logic_OnHold(state, cfg, elapsed_ms);
    }
}

/*==============================================================================
 * 接口实现
 *============================================================================*/
/**
 * @brief 初始化：把上电时的读数当成已确认状态
 * @note 这样上电那一刻不会误报 DOWN；上电就按住不放的键，
 *       会从按住计时开始，该报 LONG 就报 LONG。
 *       队列由 CubeMX 在 MX_FREERTOS_Init() 里创建，本函数不管它。
 */
void Svc_Key_Logic_Init(void)
{
    uint8_t index;

    for (index = 0u; index < (uint8_t)SVC_KEY_CFG_COUNT; ++index) {
        key_state_t *state   = &s_key_state[index];
        uint8_t      reading = Dev_Key_Read(s_key_cfg_table[index].key);

        state->reading    = reading;
        state->pressed    = reading;
        state->stable_ms  = s_key_cfg_table[index].debounce_ms;
        state->hold_ms    = 0u;
        state->repeat_ms  = 0u;
        state->long_fired = 0u;
    }
}

/**
 * @brief 周期处理：逐个按键推进一步
 * @param elapsed_ms 距上次调用的毫秒数，0 会被当成 1
 */
void Svc_Key_Logic_Tick(uint16_t elapsed_ms)
{
    uint8_t index;

    if (elapsed_ms == 0u) {
        elapsed_ms = 1u;
    }

    for (index = 0u; index < (uint8_t)SVC_KEY_CFG_COUNT; ++index) {
        Svc_Key_Logic_Step(&s_key_state[index], &s_key_cfg_table[index], elapsed_ms);
    }
}
