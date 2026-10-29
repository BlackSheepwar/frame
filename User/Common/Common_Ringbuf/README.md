# Common_Ringbuf

结构体环形缓冲（FIFO），纯 C 实现、无外部依赖，按"整条单元"存取，容量按单元数计。

- 头文件：`com_ringbuf.h`；实现：`com_ringbuf.c`；CMake 库：`common_ringbuf`
- 控制块 `ringbuf_t` 是完整类型，可直接内嵌进调用方对象，存储区由调用方提供，全程无动态分配
- 空/满判定：`count == 0` 为空，`count == capacity` 为满，不牺牲槽位

## 接口

| 接口 | 说明 |
| --- | --- |
| `Common_Ringbuf_Init(q, mem, capacity, item_size)` | 绑定存储区并清空队列；校验参数与乘积溢出 |
| `Common_Ringbuf_Push(q, item)` | 入队一条；满返回 `CRB_ERR_FULL`，`item` 为 NULL 返回 `CRB_ERR_NULL` |
| `Common_Ringbuf_Pop(q, item)` | 出队一条；空返回 `CRB_ERR_EMPTY`，`item` 为 NULL 表示丢弃 |
| `Common_Ringbuf_Reset(q)` | 丢弃全部数据并归零读写下标 |
| `Common_Ringbuf_Count(q)` / `Common_Ringbuf_Free(q)` | 当前条数 / 剩余容量快照 |

状态码：`CRB_OK`、`CRB_ERR_NULL`、`CRB_ERR_PARAM`、`CRB_ERR_FULL`、`CRB_ERR_EMPTY`。

## 并发约定（必须遵守）

库内部不加锁，`head/tail/count` 的读改写都不是原子操作。**只要 Push 与 Pop 可能同时
执行，调用方就必须把一侧串行化**：

- 任务里 Push、中断里 Pop：把 Push 整个包进 `taskENTER_CRITICAL()/taskEXIT_CRITICAL()`，
  或关掉该中断；Pop 在中断里天然不会与任务并发。
- 两个任务一读一写：两侧各自加互斥量或临界区。
- 多个任务都 Push：必须互斥，否则 `count++` 会丢失更新，队列状态被永久破坏。
- `Common_Ringbuf_Reset()` 要写三个字段，只能在确认没有并发访问时调用。

想把 Push 直接放进 ISR、Pop 放在任务里而不加任何保护，是不成立的——本实现不是
无锁 SPSC 队列，`count` 的加减必须互不重叠。

## 示例 1：静态内嵌（嵌入式推荐）

```c
#include "com_ringbuf.h"

typedef struct {
    int   id;
    float value;
} SensorData_t;

typedef struct {
    ringbuf_t    rb;
    SensorData_t buffer[8];   /* 存储区：8 条单元 */
} SensorQueue_t;

static SensorQueue_t s_sensor_queue;

void Sensor_Init(void)
{
    (void)Common_Ringbuf_Init(&s_sensor_queue.rb,
                              (uint8_t *)s_sensor_queue.buffer,
                              8,
                              sizeof(SensorData_t));
}

void Sensor_Push(SensorData_t *data)
{
    (void)Common_Ringbuf_Push(&s_sensor_queue.rb, data);
}

void Sensor_Pop(void)
{
    SensorData_t received;

    if (Common_Ringbuf_Pop(&s_sensor_queue.rb, &received) == CRB_OK) {
        /* 使用 received.id / received.value */
    }
}
```

## 示例 2：在任务里消费，中断里生产

```c
static Message_t s_msg_pool[16];
static ringbuf_t  s_msg_queue;

void Message_Init(void)
{
    (void)Common_Ringbuf_Init(&s_msg_queue, (uint8_t *)s_msg_pool,
                              16, sizeof(Message_t));
}

/* 任务上下文：入队，整段包在临界区里 */
void Message_Send(const Message_t *msg)
{
    taskENTER_CRITICAL();
    (void)Common_Ringbuf_Push(&s_msg_queue, msg);
    taskEXIT_CRITICAL();
}

/* 中断上下文：出队，queue 里只做搬数据，不调 FreeRTOS API */
void Message_RxISR(void)
{
    Message_t msg;

    if (Common_Ringbuf_Pop(&s_msg_queue, &msg) == CRB_OK) {
        /* 交给任务处理：只记标志 / 发信号量，不在 ISR 里做业务 */
    }
}
```

## 常见坑

- `item` 不能指向本队列存储区内部，否则 `memcpy` 源目重叠是未定义行为。
- `item_size` 必须等于实际结构体大小，混用不同大小会数据错位。
- `mem` 的生命周期要覆盖整个使用期；`Common_Ringbuf_Init` 不会复制存储区。
- 判断"能不能再 Push"直接看 `Push` 的返回值，不要先 `Free()` 再 `Push()`——两步之间
  可能被其他写者插队。
- `Common_Ringbuf_Count()` / `Common_Ringbuf_Free()` 只是瞬时快照，不能当作后续操作的保证。
