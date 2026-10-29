/**
 * @file com_ringbuf.c
 * @brief 结构体环形缓冲实现（纯 C，无外部依赖）
 * @author 李嘉图
 * @date 2026-10-27
 *
 * @note 用 head/tail/count 三者描述队列状态：count == 0 为空，
 *       count == capacity 为满，head 与 tail 在 [0, capacity) 内回绕。
 *       内部不加锁：并发访问必须由调用方串行化，详见 com_ringbuf.h 的
 *       @warning。
 */

/*==============================================================================
 * 头文件包含
 *============================================================================*/
// 固定包含
#include "com_ringbuf.h"

// 功能包含
#include <string.h>

/*==============================================================================
 * 接口实现
 *============================================================================*/

/**
 * @brief  初始化队列，把队列置为空状态
 * @param  q         队列控制块，不能为 NULL
 * @param  mem       存储区，字节数必须 ≥ capacity * item_size
 * @param  capacity  最大单元数，须 > 0
 * @param  item_size 每个单元字节数，须 > 0
 * @retval CRB_OK: 初始化成功
 * @retval CRB_ERR_NULL: q 或 mem 为 NULL
 * @retval CRB_ERR_PARAM: capacity/item_size 为 0，或 capacity * item_size 溢出
 */
ringbuf_status_t Common_Ringbuf_Init(ringbuf_t *q, uint8_t *mem,
                                     size_t capacity, size_t item_size)
{
    /* ---------- 1. 参数校验：指针 ---------- */
    if (q == NULL || mem == NULL) {
        return CRB_ERR_NULL;
    }

    /* ---------- 2. 参数校验：数值 ---------- */
    if (capacity == 0 || item_size == 0) {
        return CRB_ERR_PARAM;
    }
    /* 防止 capacity * item_size 溢出，导致调用方分配的存储区实际不够 */
    if (capacity > SIZE_MAX / item_size) {
        return CRB_ERR_PARAM;
    }

    /* ---------- 3. 填充元数据，队列置空 ---------- */
    q->mem       = mem;
    q->capacity  = capacity;
    q->item_size = item_size;
    q->head      = 0;
    q->tail      = 0;
    q->count     = 0;

    return CRB_OK;
}

/**
 * @brief  入队：把一条单元复制到队列尾部
 * @param  q    队列句柄，不能为 NULL
 * @param  item 待入队的单元，不能为 NULL
 * @retval CRB_OK: 入队成功
 * @retval CRB_ERR_NULL: q 或 item 为 NULL
 * @retval CRB_ERR_FULL: 队列已满，未写入任何数据
 */
ringbuf_status_t Common_Ringbuf_Push(ringbuf_t *q, const void *item)
{
    /* ---------- 1. 参数校验 ---------- */
    if (q == NULL || item == NULL) {
        return CRB_ERR_NULL;
    }

    /* ---------- 2. 容量检查，满则直接拒绝 ---------- */
    if (q->count >= q->capacity) {
        return CRB_ERR_FULL;
    }

    /* ---------- 3. 复制单元到写指针位置 ---------- */
    memcpy(q->mem + q->tail * q->item_size, item, q->item_size);

    /* ---------- 4. 写指针回绕并累加计数 ---------- */
    q->tail++;
    if (q->tail >= q->capacity) {
        q->tail = 0;
    }
    q->count++;

    return CRB_OK;
}

/**
 * @brief  出队：把队头单元复制到目标缓冲
 * @param  q    队列句柄，不能为 NULL
 * @param  item 接收缓冲；传 NULL 表示只丢弃队头一条数据
 * @retval CRB_OK: 出队成功
 * @retval CRB_ERR_NULL: q 为 NULL
 * @retval CRB_ERR_EMPTY: 队列为空，item 未被改写
 */
ringbuf_status_t Common_Ringbuf_Pop(ringbuf_t *q, void *item)
{
    /* ---------- 1. 参数校验 ---------- */
    if (q == NULL) {
        return CRB_ERR_NULL;
    }

    /* ---------- 2. 空检查，空队列没有数据可出 ---------- */
    if (q->count == 0) {
        return CRB_ERR_EMPTY;
    }

    /* ---------- 3. 复制队头单元（item 为 NULL 时只丢弃） ---------- */
    if (item != NULL) {
        memcpy(item, q->mem + q->head * q->item_size, q->item_size);
    }

    /* ---------- 4. 读指针回绕并递减计数 ---------- */
    q->head++;
    if (q->head >= q->capacity) {
        q->head = 0;
    }
    q->count--;

    return CRB_OK;
}

/**
 * @brief  清空队列，丢弃全部尚未取走的单元
 * @param  q 队列句柄，不能为 NULL
 * @retval 无
 */
void Common_Ringbuf_Reset(ringbuf_t *q)
{
    /* ---------- 1. 参数校验 ---------- */
    if (q == NULL) {
        return;
    }

    /* ---------- 2. 读写下标与计数一起归零 ---------- */
    q->head  = 0;
    q->tail  = 0;
    q->count = 0;
}

/**
 * @brief  获取当前单元数
 * @param  q 队列句柄，不能为 NULL
 * @retval 0..capacity: 当前队列中的单元数；q 为 NULL 时返回 0
 */
size_t Common_Ringbuf_Count(const ringbuf_t *q)
{
    /* ---------- 1. 参数校验 ---------- */
    if (q == NULL) {
        return 0;
    }

    /* ---------- 2. 返回计数快照 ---------- */
    return q->count;
}

/**
 * @brief  获取剩余容量
 * @param  q 队列句柄，不能为 NULL
 * @retval 0..capacity: 还能再入队多少条单元；q 为 NULL 时返回 0
 */
size_t Common_Ringbuf_Free(const ringbuf_t *q)
{
    /* ---------- 1. 参数校验 ---------- */
    if (q == NULL) {
        return 0;
    }

    /* ---------- 2. 计数异常时按满处理，避免无符号下溢 ---------- */
    if (q->count >= q->capacity) {
        return 0;
    }

    /* ---------- 3. 返回剩余容量快照 ---------- */
    return q->capacity - q->count;
}
