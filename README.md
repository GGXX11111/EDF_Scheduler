# EDF — Event Driven Framework

面向嵌入式 MCU 的事件驱动框架。中断只置事件，业务在主循环处理；全静态分配，无动态内存、无独立任务栈。

## 接入

```
Inc/    edf.h（聚合头，只需包含它）、edf_base.h、edf_{task,timer,msg,stream}_mgr.h
Src/    edf_base.c（HAL 移植层）、edf_{task,timer,msg,stream}_mgr.c
```

把 `Inc` 加入头文件路径，`Src` 下全部 `.c` 加入编译。

## 快速开始

```c
#include "edf.h"

#define EVT_TIMEOUT     (1U << 0)
#define EVT_UART_RX     (1U << 1)

TASK_DEFINE(task_app);
TIMER_DEFINE(timer_tick);
STREAM_DEFINE(stream_rx);

static uint8_t g_rx_buf[256];

static void app_init(task_t *task, void *data)
{
    (void)task; (void)data;
    /* 外设初始化 */
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

    return event;   /* 返回未处理的事件位 */
}

int main(void)
{
    task_init(&task_app, app_init, app_handler, TASK_PRIORITY_NORMAL, NULL);
    stream_init(&stream_rx, g_rx_buf, sizeof(g_rx_buf), &task_app, EVT_UART_RX);

    timer_init(&timer_tick, TIMER_MODE_PERIODIC, TIMER_MS(500), &task_app, EVT_TIMEOUT);
    timer_start(&timer_tick);

    task_scheduler_start();   /* 永不返回 */
}
```

中断侧：

```c
void USART1_IRQHandler(void)
{
    uint8_t byte = USART1->DR;
    hal_size_t written;
    stream_write(&stream_rx, &byte, 1, &written);
}
```

## API

### 任务

| 函数 | 说明 | 中断可用 |
| --- | --- | :---: |
| `task_init` | 初始化任务，内部立即执行 `init_func` | ✗ |
| `task_set_event` | 置位事件，必要时入就绪队列 | ✓ |
| `task_clear_event` | 清除事件位，清空时出就绪队列 | ✓ |
| `task_scheduler_start` | 启动调度循环，不返回 | — |

`handler` 返回**未处理的事件位**，调度器合并回任务并重新入队。禁止返回 `EVENT_FULL`。

### 定时器

| 函数 | 说明 | 中断可用 |
| --- | --- | :---: |
| `timer_init` | 初始化（单次 / 周期，周期不能为 0） | ✗ |
| `timer_start` | 启动 | ✓ |
| `timer_stop` | 停止 | ✓ |
| `timer_set_mode` | 改单次 / 周期 | ✓ |
| `timer_set_reload` | 改周期，运行中立即重新计时 | ✓ |

到期**只置位绑定任务的事件**，不执行回调。`TIMER_MS()` / `TIMER_SEC()` 换算 tick。单次定时器到期后停在 `EXPIRED`，需 `timer_start()` 复用；`notify_task` 为 `NULL` 时到期无任何通知。

### 消息队列

| 函数 | 说明 | 中断可用 |
| --- | --- | :---: |
| `msg_queue_init` | 初始化，`capacity` 必须为 2 的幂 | ✗ |
| `msg_queue_send` | 发送定长消息，满则返回 `EDF_ERR`（不覆盖） | ✓ |
| `msg_queue_recv` | 接收，空则返回 `EDF_ERR` | ✓ |
| `msg_queue_peek` | 窥视队首，不消费 | ✓ |
| `msg_queue_get_count` / `msg_queue_flush` | 计数 / 清空 | ✓ |

### 字节流

| 函数 | 说明 | 中断可用 |
| --- | --- | :---: |
| `stream_init` | 初始化，`capacity` 为 2 的幂，`buffer` 4 字节对齐 | ✗ |
| `stream_write` / `stream_read` | 拷贝式读写，可部分完成，实际长度由出参返回 | ✓ |
| `stream_peek` | 窥视，不消费 | ✓ |
| `stream_get_write_ptr` + `stream_advance_write` | 零拷贝写（DMA 场景） | ✓ |
| `stream_get_read_ptr` + `stream_advance_read` | 零拷贝读 | ✓ |
| `stream_get_used` / `stream_flush` | 已用字节数 / 清空 | ✓ |

## 移植

**1. 临界区**：`edf_base.h` 中的宏默认都是空实现，需替换为真实实现（Cortex-M 示例，三种任选其一）。

| 方式 | 屏蔽级别 | 屏蔽范围 |
| --- | --- | --- |
| 保存 / 恢复 PRIMASK | PRIMASK | 所有可屏蔽中断 |
| 嵌套计数 + 开关中断 | PRIMASK | 所有可屏蔽中断 |
| 保存 / 恢复 BASEPRI | BASEPRI | 仅优先级数值 ≥ 阈值的中断 |

方式一：保存 / 恢复 PRIMASK

```c
#define HAL_ENTER_CRITICAL_SECTION()    uint32_t __pm = __get_PRIMASK(); __disable_irq()
#define HAL_EXIT_CRITICAL_SECTION()     __set_PRIMASK(__pm)
```

方式二：嵌套计数 + 开关中断

```c
#define HAL_DISABLE_INTERRUPTS()    __disable_irq()
#define HAL_ENABLE_INTERRUPTS()     __enable_irq()

#define HAL_ENTER_CRITICAL_SECTION()                                    \
    do {                                                                \
        if (g_critical_count == 0U) { HAL_DISABLE_INTERRUPTS(); }       \
        g_critical_count++;                                             \
    } while (0)

#define HAL_EXIT_CRITICAL_SECTION()                                     \
    do {                                                                \
        if (g_critical_count > 0U) {                                    \
            g_critical_count--;                                         \
            if (g_critical_count == 0U) { HAL_ENABLE_INTERRUPTS(); }    \
        }                                                               \
    } while (0)
```

方式三：保存 / 恢复 BASEPRI

```c
#define EDF_SYSCALL_PRIO    (5U)    /* 优先级数值 >= 5 的中断才会被临界区屏蔽 */

#define HAL_ENTER_CRITICAL_SECTION()                                    \
    uint32_t __bpm = __get_BASEPRI();                                   \
    __set_BASEPRI(EDF_SYSCALL_PRIO << (8U - __NVIC_PRIO_BITS))

#define HAL_EXIT_CRITICAL_SECTION()     __set_BASEPRI(__bpm)
```

选择 BASEPRI 时，所有调用框架 API 的中断（含定时器 ISR）优先级数值必须 ≥ `EDF_SYSCALL_PRIO`，且优先级高于阈值的中断不得访问框架数据。

三种方式都必须支持嵌套。

**2. 硬件定时器**：在 `Src/edf_base.c` 实现 `hal_timer_init/start/stop`，中断入口：

```c
void hal_timer_isr(void)
{
    hal_timer_clear_irq_flag();
    timer_update();
}
```

当前 `edf_base.c` 中这四个函数均为空实现，联调前必须补全。

**3. 配置宏**：`EDF_TICK_FREQ_HZ`（默认 1000）、`EDF_IDLE_HOOK()`（无就绪任务时调用，可放低功耗）。

## 注意

1. `handler` 非抢占，必须快速返回；禁止死循环、忙等、阻塞调用。
2. `*_init` 类函数禁止在中断中调用，其余运行期接口均中断安全。
3. 控制块必须零初始化，用 `TASK_DEFINE` / `TIMER_DEFINE` / `MSGQ_DEFINE` / `STREAM_DEFINE`。
4. 控制块创建后长期存在，没有销毁接口。
5. 定时器 tick 为相对递减：关中断超过一个 tick 周期会丢 tick 且不会自愈，需控制临界区时长。
