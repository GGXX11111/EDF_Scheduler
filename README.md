# EDF — Event Driven Framework

面向深度嵌入式 MCU 的轻量级事件驱动框架。中断只负责置事件，业务逻辑统一在主循环中处理，无动态内存、无独立任务栈、无可嵌套调度。

- **全静态分配**：所有控制块由调用者提供并零初始化，运行期不调用 `malloc`
- **事件驱动**：32 位事件位图驱动任务调度，无事件不空转
- **固定优先级**：5 级优先级（数值越小越高），位图查找 + 同级 FIFO，非抢占
- **中断安全**：明确的 API 中断安全标注，临界区支持嵌套计数
- **基础组件**：任务调度、软件定时器、定长消息队列、字节流（环形缓冲 / 零拷贝）

---

## 目录结构

```
EDF_Scheduler/
├── Inc/
│   ├── edf.h                 # 聚合头文件，只需包含这一个
│   ├── edf_base.h            # 配置项、状态码、临界区宏、硬件抽象接口
│   ├── edf_task_mgr.h        # 任务管理
│   ├── edf_timer_mgr.h       # 软件定时器
│   ├── edf_msg_mgr.h         # 定长消息队列
│   └── edf_stream_mgr.h      # 字节流
└── Src/
    ├── edf_base.c            # 硬件抽象层（移植时重写这里）
    ├── edf_task_mgr.c
    ├── edf_timer_mgr.c
    ├── edf_msg_mgr.c
    └── edf_stream_mgr.c
```

接入工程时只需把 `Inc` 加入头文件搜索路径，把 `Src` 下所有 `.c` 加入编译。

---

## 架构

```
        中断上下文                      调度器上下文
  ┌──────────────────┐          ┌──────────────────────────┐
  │  UART ISR        │          │  task_scheduler_start()  │
  │    ↓             │          │    ↓                     │
  │  stream_write()  │─事件─┐   │  get_next_active_task()  │
  │                  │      │   │    ↓                     │
  │  定时器 ISR      │      │   │  task_handler_func()     │
  │    ↓             │      ├──→│    ↓                     │
  │  timer_update()  │      │   │  读取 msg / stream       │
  │                  │      │   │  处理业务                │
  │  task_set_event()│──────┘   │  返回未处理事件 → 重新入队│
  └──────────────────┘          └──────────────────────────┘
```

关键点：**中断里不执行业务逻辑，只置事件**。任务的 handler 由调度器在主循环上下文中调用，因此可以安全地访问共享数据、调用阻塞式外设驱动。

---

## 核心概念

### 任务（task）

任务没有独立栈，也没有上下文切换。`handler_func` 在调度器上下文中执行，返回后控制权回到调度循环。

```c
task_t *task_init(task_t *task,
                  fp_task_init_func    init_func,     /* 可为 NULL */
                  fp_task_handler_func handler_func,  /* 不可为 NULL */
                  task_priority_t      priority,
                  void                *task_data);    /* 可为 NULL */
```

`init_func` 在 `task_init()` 内部立即调用一次，适合做外设初始化。

### 事件（event）

`task_event_t` 是 32 位位图。handler 收到的是**事件快照**，返回值为**仍未处理的事件位**，调度器会把它合并回任务并重新入队：

```c
static task_event_t my_handler(task_t *task, task_event_t event, void *data)
{
    if (TASK_EVENT_CHECK(event, EVT_A))
    {
        TASK_EVENT_CLEAR(event, EVT_A);   /* 标记已处理 */
        /* ... */
    }
    return event;                          /* 剩余未处理的事件 */
}
```

> 禁止返回 `EVENT_FULL`，否则该任务会被无限重新入队，饿死其他任务。

### 定时器（timer）

基于 **delta 链表**的软件定时器。到期时**只置位绑定任务的事件**，不执行任何回调，中断上下文不做业务处理。

```c
timer_t *timer_init(timer_t *timer,
                    timer_mode_t  timer_mode,     /* 单次 / 周期 */
                    timer_tick_t  timer_reload,   /* 周期，单位 tick，不能为 0 */
                    task_t       *notify_task,    /* 到期通知的任务 */
                    task_event_t  notify_event);
```

- `TIMER_MS(500)` / `TIMER_SEC(1)` 用于换算 tick 数，精度为 `1 / EDF_TICK_FREQ_HZ`（默认 1ms）
- `notify_task` 为 `NULL` 时定时器到期只更新自身状态，不会产生任何通知
- 单次定时器到期后停在 `TIMER_STATUS_EXPIRED`，需显式 `timer_start()` 复用
- 链表中没有活动定时器时自动关闭硬件节拍，避免 ISR 空转

### 消息队列（msg_queue）

定长消息的环形队列，`capacity` 必须是 **2 的幂**（内部用掩码取模）。队列满时 `msg_queue_send()` 返回 `EDF_ERR`，**不覆盖**旧数据。

### 字节流（stream）

字节级环形缓冲，支持两种用法：

1. **拷贝式**：`stream_write()` / `stream_read()`，空间不足时只处理能容纳的部分
2. **零拷贝式**：`stream_get_write_ptr()` + `stream_advance_write()`，适合 DMA 或大批量收发

`capacity` 必须为 2 的幂，`buffer` 需 4 字节对齐。

---

## 快速开始

```c
#include "edf.h"

/* ---- 事件位定义 ---- */
#define EVT_TIMEOUT     (1U << 0)
#define EVT_UART_RX     (1U << 1)

/* ---- 静态控制块 ---- */
TASK_DEFINE(task_app);
TIMER_DEFINE(timer_tick);
STREAM_DEFINE(stream_rx);

static uint8_t g_rx_buf[256];

static void app_init(task_t *task, void *data)
{
    (void)task; (void)data;
    /* 外设初始化：GPIO、UART 等 */
}

static task_event_t app_handler(task_t *task, task_event_t event, void *data)
{
    (void)task; (void)data;

    if (TASK_EVENT_CHECK(event, EVT_TIMEOUT))
    {
        TASK_EVENT_CLEAR(event, EVT_TIMEOUT);
        /* 500ms 周期任务 */
    }

    if (TASK_EVENT_CHECK(event, EVT_UART_RX))
    {
        uint8_t byte;
        hal_size_t readed;

        while ((stream_read(&stream_rx, &byte, 1, &readed) == EDF_OK) && (readed == 1U))
        {
            /* 逐字节处理 */
        }
        TASK_EVENT_CLEAR(event, EVT_UART_RX);
    }

    return event;
}

int main(void)
{
    task_init(&task_app, app_init, app_handler, TASK_PRIORITY_NORMAL, NULL);

    stream_init(&stream_rx, g_rx_buf, sizeof(g_rx_buf), &task_app, EVT_UART_RX);

    timer_init(&timer_tick, TIMER_MODE_PERIODIC, TIMER_MS(500), &task_app, EVT_TIMEOUT);
    timer_start(&timer_tick);

    task_scheduler_start();   /* 永不返回 */
    return 0;
}
```

中断侧：

```c
void USART1_IRQHandler(void)
{
    uint8_t byte = USART1->DR;
    hal_size_t written;

    stream_write(&stream_rx, &byte, 1, &written);   /* 中断安全 */
}
```

---

## API 速查

### 任务管理

| 函数 | 说明 | 中断可调用 |
| --- | --- | :---: |
| `task_init` | 初始化任务（静态创建） | ✗ |
| `task_set_event` | 置位事件，必要时加入就绪队列 | ✓ |
| `task_clear_event` | 清除事件位，事件清空时移出就绪队列 | ✓ |
| `task_scheduler_start` | 启动调度循环，不返回 | — |

### 软件定时器

| 函数 | 说明 | 中断可调用 |
| --- | --- | :---: |
| `timer_init` | 初始化定时器 | ✗ |
| `timer_start` | 启动定时器 | ✓ |
| `timer_stop` | 停止定时器 | ✓ |
| `timer_set_mode` | 修改单次/周期模式 | ✓ |
| `timer_set_reload` | 修改周期，运行中立即重新计时 | ✓ |

### 消息队列

| 函数 | 说明 | 中断可调用 |
| --- | --- | :---: |
| `msg_queue_init` | 初始化队列 | ✗ |
| `msg_queue_send` | 发送一条定长消息（满则失败） | ✓ |
| `msg_queue_recv` | 接收一条消息（空则失败） | ✓ |
| `msg_queue_peek` | 窥视队首，不消费 | ✓ |
| `msg_queue_get_count` | 获取当前消息数 | ✓ |
| `msg_queue_flush` | 清空队列 | ✓ |

### 字节流

| 函数 | 说明 | 中断可调用 |
| --- | --- | :---: |
| `stream_init` | 初始化字节流 | ✗ |
| `stream_write` | 写入字节，可能部分写入 | ✓ |
| `stream_read` | 读取字节，可能部分读取 | ✓ |
| `stream_peek` | 窥视字节，不消费 | ✓ |
| `stream_get_write_ptr` | 获取连续可写区域（零拷贝） | ✓ |
| `stream_advance_write` | 提交已写入的字节数 | ✓ |
| `stream_get_read_ptr` | 获取连续可读区域（零拷贝） | ✓ |
| `stream_advance_read` | 提交已消费的字节数 | ✓ |
| `stream_get_used` | 获取已用字节数 | ✓ |
| `stream_flush` | 清空字节流 | ✓ |

---

## 移植指南

### 1. 临界区

框架的线程安全完全建立在 `HAL_DISABLE_INTERRUPTS()` / `HAL_ENABLE_INTERRUPTS()` 之上。`edf_base.h` 中默认是**空实现**，不定义就等于没有保护。

必须在包含 `edf.h` 之前定义（Cortex-M 示例）：

```c
#define HAL_DISABLE_INTERRUPTS()    __disable_irq()
#define HAL_ENABLE_INTERRUPTS()     __enable_irq()
#include "edf.h"
```

要求：

- 必须是 **PRIMASK 级别**（屏蔽所有可屏蔽中断），不能只改 BASEPRI
- 不能嵌套保存/恢复编译器状态，框架已用 `g_critical_count` 处理嵌套
- ISR 中调用同样有效（PRIMASK 在异常上下文中可用）

### 2. 硬件定时器

在 `Src/edf_base.c` 中实现以下四个函数：

| 函数 | 职责 |
| --- | --- |
| `hal_timer_init(tick_freq)` | 配置硬件定时器，周期 = `1 / tick_freq` 秒 |
| `hal_timer_start()` | 启动计数并允许中断 |
| `hal_timer_stop()` | 停止计数并禁止中断 |
| `hal_timer_isr()` | 中断入口，由 `XXX_IRQHandler` 调用 |

`hal_timer_isr()` 的标准写法：

```c
void hal_timer_isr(void)
{
    hal_timer_clear_irq_flag();   /* 必须先清标志，否则会中断风暴 */
    timer_update();
}
```

> 注意：当前 `Src/edf_base.c` 中的四个函数都是空实现（直接返回 `EDF_OK`），联调前必须补全，尤其是清中断标志这一步。

### 3. 可配置项

| 宏 | 位置 | 默认值 | 说明 |
| --- | --- | --- | --- |
| `EDF_TICK_FREQ_HZ` | `edf_base.h` | `1000` | 定时器节拍频率（Hz） |
| `EDF_IDLE_HOOK()` | `edf_base.h` | 空 | 无就绪任务时调用，可放低功耗指令 |

---

## 使用约束

1. **handler 必须快速返回**。它是非抢占的，一个 handler 跑多久，其他任务就等多久。禁止死循环、禁止忙等、禁止调用可能阻塞的接口。
2. **禁止在 handler 内递归调用 `task_scheduler_start()`**。
3. **`task_init` / `timer_init` / `msg_queue_init` / `stream_init` 禁止在中断上下文调用**，其余运行期接口均中断安全。
4. **控制块必须零初始化**。使用 `TASK_DEFINE` / `TIMER_DEFINE` / `MSGQ_DEFINE` / `STREAM_DEFINE` 宏，或 `= {0}`。
5. **控制块没有销毁接口**，创建后长期存在。
6. **不要返回 `EVENT_FULL`**，会导致任务无限重新入队。
7. **`capacity` 必须是 2 的幂**（消息队列与字节流均通过掩码取模）。
8. **定时器 tick 采用相对递减**。若某次关中断时长超过一个 tick 周期，会丢失 tick 且不会自愈，表现为定时器整体变慢。请控制临界区与中断关闭时长。

---

## 已知限制

- 仅支持 5 级固定优先级，无时间片轮转，无优先级继承
- 非抢占调度，handler 之间不会互相打断
- 单核设计，未考虑 SMP 场景下的跨核同步
- 定时器到期不执行回调，实时动作需在任务 handler 中完成，延迟取决于调度时机
- 无任务/定时器的运行时创建与销毁，全部静态定义
