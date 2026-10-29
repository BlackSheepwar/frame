/**
 * @file com_ringbuf.h
 * @brief 结构体环形缓冲（纯 C 实现，无外部依赖）
 * @author 李嘉图
 * @date 2026-10-27
 *
 * @note 存取单位是"整条单元"，容量按单元数计，不按字节。
 *       head/tail 分别是下一次读/写的下标，count 负责区分空与满
 *       （head == tail 时靠 count 判断），因此不需要牺牲一个槽位来标记满。
 *
 * @warning 本模块内部不加锁。Push 与 Pop 一旦可能并发执行，必须由调用方
 *          串行化：典型场景"任务里 Push、中断里 Pop"要把 Push 一侧包进
 *          临界区/关中断，使两边的 count 读改写互不重叠；否则 count 会
 *          丢失更新，队列状态被永久破坏。多生产者或多消费者同样由调用方
 *          保证互斥。用法示例见同目录 README.md。
 */

#ifndef __Common_Ringbuf_H__
#define __Common_Ringbuf_H__

/*==============================================================================
 * 头文件包含
 *============================================================================*/
// 固定包含
#include <stddef.h>
#include <stdint.h>

/*==============================================================================
 * 类型定义
 *============================================================================*/
/**
 * @brief 队列状态码
 */
typedef enum {
    CRB_OK        =  0, /* 操作成功 */
    CRB_ERR_NULL  = -1, /* 句柄或数据指针为 NULL */
    CRB_ERR_PARAM = -2, /* 容量或单元长度为 0，或 capacity * item_size 溢出 */
    CRB_ERR_FULL  = -3, /* 队列已满，无法入队 */
    CRB_ERR_EMPTY = -4, /* 队列为空，无法出队 */
} ringbuf_status_t;

/**
 * @brief 队列控制块
 * @note  完整类型，调用方可以把 ringbuf_t 直接内嵌进自己的对象（BSP 的
 *        总线句柄就是这么做的），省掉一次指针间接与一次动态分配。
 *        字段属于实现细节：读写一律走 Common_Ringbuf_* 接口，不要直接
 *        改 head/tail/count，否则空/满判定立刻失效。
 * @warning item 不得指向本队列存储区内部，否则 memcpy 源目重叠触发 UB。
 */
typedef struct {
    uint8_t *mem;          /* 存储区，字节数 = capacity * item_size */
    size_t   capacity;     /* 最大单元数 */
    size_t   item_size;    /* 每个单元字节数 */
    volatile size_t head;  /* 读下标：下一次 Pop 的位置 */
    volatile size_t tail;  /* 写下标：下一次 Push 的位置 */
    volatile size_t count; /* 当前单元数，用于区分空与满 */
} ringbuf_t;

/*==============================================================================
 * 接口声明
 *============================================================================*/
/**
 * @brief 初始化队列（同时清空队列内容）
 * @param q         队列控制块，不能为 NULL
 * @param mem       存储区，字节数 ≥ capacity * item_size，生命周期由调用方保证
 * @param capacity  最大单元数（队列容量），须 > 0
 * @param item_size 每个单元的字节数，须 > 0
 * @retval CRB_OK: 初始化成功，队列变为空
 * @retval CRB_ERR_NULL: q 或 mem 为 NULL
 * @retval CRB_ERR_PARAM: capacity 或 item_size 为 0，或两者乘积溢出
 * @note 可重复调用，但必须保证调用时没有并发的 Push/Pop；重复调用会丢弃
 *       队列里尚未取走的数据。
 */
ringbuf_status_t Common_Ringbuf_Init(ringbuf_t *q, uint8_t *mem,
                                     size_t capacity, size_t item_size);

/**
 * @brief 入队（把一条单元复制到队列尾部）
 * @param q    队列句柄，不能为 NULL
 * @param item 待入队的单元，不能为 NULL
 * @retval CRB_OK: 入队成功
 * @retval CRB_ERR_NULL: q 或 item 为 NULL
 * @retval CRB_ERR_FULL: 队列已满，本次没有写入任何数据
 * @note item 不得指向本队列存储区内部，否则 memcpy 源目重叠触发 UB。
 */
ringbuf_status_t Common_Ringbuf_Push(ringbuf_t *q, const void *item);

/**
 * @brief 出队（把队头单元复制到目标缓冲）
 * @param q    队列句柄，不能为 NULL
 * @param item 接收缓冲，不能为 NULL；传 NULL 只丢弃队头一条数据
 * @retval CRB_OK: 出队成功
 * @retval CRB_ERR_NULL: q 为 NULL
 * @retval CRB_ERR_EMPTY: 队列为空，item 未被改写
 */
ringbuf_status_t Common_Ringbuf_Pop(ringbuf_t *q, void *item);

/**
 * @brief 清空队列（丢弃全部单元，重置读写下标与计数）
 * @param q 队列句柄，不能为 NULL
 * @retval 无
 * @note Reset 会同时写 head/tail/count，必须保证调用时没有并发的 Push/Pop，
 *       否则计数会被写坏。
 */
void Common_Ringbuf_Reset(ringbuf_t *q);

/**
 * @brief 获取当前单元数
 * @param q 队列句柄，不能为 NULL
 * @retval 0..capacity: 当前队列中的单元数
 * @note 返回值是瞬时快照；在中断/任务并发写入时，下一次调用可能已经变化。
 */
size_t Common_Ringbuf_Count(const ringbuf_t *q);

/**
 * @brief 获取剩余容量
 * @param q 队列句柄，不能为 NULL
 * @retval 0..capacity: 还能再入队多少条单元
 * @note 返回值是瞬时快照，判断"能否再 Push"应直接看 Push 的返回值，不要
 *       先 Free 再 Push，两步之间可能被其他写者插队。
 */
size_t Common_Ringbuf_Free(const ringbuf_t *q);

#endif /* __Common_Ringbuf_H__ */
