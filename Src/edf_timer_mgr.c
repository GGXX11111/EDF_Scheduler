/**
 ******************************************************************************
 * @file    edf_timer_mgr.c
 * @brief   定时器管理器实现
 ******************************************************************************
 */

/* Includes ------------------------------------------------------------------*/

#include "edf_timer_mgr.h"
#include "edf_base.h"
#include "edf_mem_mgr.h"

/* Private constants ---------------------------------------------------------*/

/* Private types -------------------------------------------------------------*/

/* 定时器控制块定义 */
struct timer_blk
{
    struct timer_blk *next;         // 链表下一个节点
    struct timer_blk *prev;         // 链表上一个节点
    timer_mode_t timer_mode;        // 定时器模式
    timer_status_t timer_status;    // 定时器状态
	uint8_t reserved0[2];
    timer_tick_t timer_reload;      // 重载值（周期）
    timer_tick_t delta_ticks;       // 距离前一个定时器到期的剩余节拍数（Delta List）
    task_t *notify_task;            // 超时时通知的任务
    task_event_t notify_event;      // 超时时触发的事件标志
};

/* 软件定时器双向链表 */
typedef struct timer_list
{
    timer_t *head;                  // 链表头指针（NULL表示空链表）
} timer_list_t;

/* Private variables ---------------------------------------------------------*/

/* 系统节拍计数器 */
static volatile timer_tick_t g_tick_count = 0;

/* 硬件定时器激活状态 */
static timer_status_t g_timer_hw_active = TIMER_HW_INACTIVE;

/* 全局软件定时器双向链表 */
static timer_list_t g_timer_list = { .head = (timer_t*)NULL };

/* Private defines -----------------------------------------------------------*/

/* Private function prototypes ----------------------------------------------*/

/* 静态函数声明 */
static void dlist_remove(timer_t *timer);
static void dlist_push_back(timer_t *timer);
static void delta_insert(timer_t *timer);
static void delta_splice_out(timer_t *timer);
static void hw_timer_init(timer_tick_t tick_freq);
static void hw_timer_start(void);
static void hw_timer_stop(void);

/* Exported constants --------------------------------------------------------*/

/* Exported variables --------------------------------------------------------*/

/* Exported functions --------------------------------------------------------*/

/* 软件定时器管理 */

/**
 * @brief 创建定时器
 */
timer_t *timer_create(timer_mode_t timer_mode,
                      timer_tick_t timer_reload,
                      task_t *notify_task,
                      task_event_t notify_event)
{
    timer_t *new_timer;

    /* 参数检查 */
    if (timer_reload == 0)
    {
        return (timer_t*)NULL;
    }

    /* 创建新定时器块 */
    new_timer = (timer_t*)mem_malloc(sizeof(timer_t));
    if (new_timer == (timer_t*)NULL)
    {
        return (timer_t*)NULL;
    }

    /* 初始化新定时器块（IDLE状态，追加到链表尾部） */
    new_timer->next = (timer_t*)NULL;
    new_timer->prev = (timer_t*)NULL;
    new_timer->timer_mode = timer_mode;
    new_timer->timer_reload = timer_reload;
    new_timer->delta_ticks = 0;
    new_timer->timer_status = TIMER_STATUS_IDLE;
    new_timer->notify_task = notify_task;
    new_timer->notify_event = notify_event;

	HAL_ENTER_CRITICAL_SECTION();

    /* 追加到链表尾部 */
    dlist_push_back(new_timer);

    HAL_EXIT_CRITICAL_SECTION();

    return new_timer;
}

/**
 * @brief 删除定时器
 */
void timer_delete(timer_t *timer)
{
    /* 参数检查 */
    if (timer == (timer_t*)NULL)
    {
        return;
    }

    HAL_ENTER_CRITICAL_SECTION();

    /* 从链表中摘除（RUNNING需要补偿后继delta，IDLE直接摘除） */
    if (timer->timer_status == TIMER_STATUS_RUNNING)
    {
        delta_splice_out(timer);
    }
    else
    {
        dlist_remove(timer);
    }

    HAL_EXIT_CRITICAL_SECTION();

    /* 释放定时器块 */
    mem_free(timer);
}

/**
 * @brief 启动定时器
 */
uint8_t timer_start(timer_t *timer)
{
    /* 参数检查 */
    if (timer == (timer_t*)NULL)
    {
        return EDF_FAILURE;
    }

    HAL_ENTER_CRITICAL_SECTION();

    /* 首次启动软件定时器时，初始化并启动硬件节拍定时器 */
    if (g_timer_hw_active == TIMER_HW_INACTIVE)
    {
        hw_timer_init(EDF_TICK_FREQ_HZ);
        hw_timer_start();
    }

    /* 如果已经在运行，则不再重新启动 */
    if (timer->timer_status == TIMER_STATUS_RUNNING)
    {
        HAL_EXIT_CRITICAL_SECTION();

        return EDF_SUCCESS;
    }

    /* IDLE状态：从链表尾部摘除 */
    dlist_remove(timer);

    /* 设置运行参数并插入 delta 链表 */
    timer->timer_status = TIMER_STATUS_RUNNING;
    timer->delta_ticks = timer->timer_reload;
    delta_insert(timer);

    HAL_EXIT_CRITICAL_SECTION();

    return EDF_SUCCESS;
}

/**
 * @brief 停止定时器
 */
uint8_t timer_stop(timer_t *timer)
{
    timer_t *pos;

    /* 参数检查 */
    if (timer == (timer_t*)NULL)
    {
        return EDF_FAILURE;
    }

    HAL_ENTER_CRITICAL_SECTION();

    /* 如果已经停止（非RUNNING），则不再重复停止 */
    if (timer->timer_status != TIMER_STATUS_RUNNING)
    {
        HAL_EXIT_CRITICAL_SECTION();

        return EDF_SUCCESS;
    }

    /* 摘除（自动补偿后继delta） */
    delta_splice_out(timer);

    /* 置为IDLE状态，追加到链表尾部 */
    timer->timer_status = TIMER_STATUS_IDLE;
    timer->delta_ticks = 0;
    dlist_push_back(timer);

    /* 若无剩余 RUNNING 定时器，停止硬件定时器 */
    pos = g_timer_list.head;
    while (pos != (timer_t*)NULL)
    {
        if (pos->timer_status == TIMER_STATUS_RUNNING)
        {
            break;      /* 还有活跃定时器，不停硬件定时器 */
        }
        pos = pos->next;
    }
    if (pos == (timer_t*)NULL && g_timer_hw_active == TIMER_HW_ACTIVE)
    {
        hw_timer_stop();
    }

    HAL_EXIT_CRITICAL_SECTION();

    return EDF_SUCCESS;
}

/**
 * @brief 定时器更新函数，应该在系统时钟中断服务程序中调用
 *
 * 处理流程：
 *   1. 递增系统 tick 计数器
 *   2. 若链表为空，快速返回
 *   3. 递减头节点 delta_ticks
 *   4. 处理所有 delta_ticks==0 的到期定时器
 */
void timer_update(void)
{
    timer_t *head;

    /* 1. 递增系统 tick 计数器 */
    g_tick_count++;

    /* 2. 无 RUNNING 定时器（链表空或头节点为IDLE），快速返回 */
    if (g_timer_list.head == (timer_t*)NULL ||
        g_timer_list.head->timer_status != TIMER_STATUS_RUNNING)
    {
        return;
    }

    /* 3. 递减头节点 delta（仅操作链表头，O(1)） */
    g_timer_list.head->delta_ticks--;

    /* 4. 处理所有在同一节拍到期的定时器（delta_ticks == 0） */
    while (g_timer_list.head != (timer_t*)NULL &&
           g_timer_list.head->timer_status == TIMER_STATUS_RUNNING &&
           g_timer_list.head->delta_ticks == 0)
    {
        head = g_timer_list.head;

        /* 触发超时事件 */
        task_set_event(head->notify_task, head->notify_event);

        if (head->timer_mode == TIMER_MODE_PERIODIC)
        {
            /* 周期定时器：摘除后重新插入 */
            delta_splice_out(head);
            head->timer_status = TIMER_STATUS_RUNNING;
            head->delta_ticks = head->timer_reload;
            delta_insert(head);
        }
        else
        {
            /* 单次定时器：摘除，置IDLE，追加到链表尾部 */
            delta_splice_out(head);
            head->timer_status = TIMER_STATUS_IDLE;
            dlist_push_back(head);
        }
    }
}

/**
 * @brief 设置定时器重载周期
 */
uint8_t timer_set_reload(timer_t *timer, timer_tick_t timer_reload)
{
    if (timer == (timer_t*)NULL || timer_reload == 0)
    {
        return EDF_FAILURE;
    }

    timer->timer_reload = timer_reload;

    return EDF_SUCCESS;
}

/**
 * @brief 重新启动定时器（重新开始计时）
 */
uint8_t timer_restart(timer_t *timer)
{
    if (timer == (timer_t*)NULL)
    {
        return EDF_FAILURE;
    }

    /* 先停止再启动，实现重新计时 */
    timer_stop(timer);

    return timer_start(timer);
}

/**
 * @brief 获取系统当前节拍计数
 */
timer_tick_t timer_get_tick(void)
{
    /* 32位无符号读是原子的，直接返回即可，无需临界区 */
    return g_tick_count;
}

/**
 * @brief 获取定时器当前状态
 */
timer_status_t timer_get_status(timer_t *timer)
{
    if (timer == (timer_t*)NULL)
    {
        return TIMER_STATUS_IDLE;
    }

    return timer->timer_status;
}

/**
 * @brief 获取定时器当前重载周期
 */
timer_tick_t timer_get_reload(timer_t *timer)
{
    if (timer == (timer_t*)NULL)
    {
        return 0;
    }

    return timer->timer_reload;
}

/**
 * @brief 获取定时器距离到期的剩余节拍数
 * @note O(N) 遍历 delta 链表累加得到真实剩余 ticks
 */
timer_tick_t timer_get_remaining(timer_t *timer)
{
    timer_tick_t remaining = 0;
    timer_t *pos;

    if (timer == (timer_t*)NULL)
    {
        return 0;
    }

    HAL_ENTER_CRITICAL_SECTION();

    /* 仅 RUNNING 状态有"剩余 ticks"概念 */
    if (timer->timer_status == TIMER_STATUS_RUNNING)
    {
        pos = g_timer_list.head;
        while (pos != (timer_t*)NULL &&
               pos->timer_status == TIMER_STATUS_RUNNING)
        {
            remaining += pos->delta_ticks;
            if (pos == timer)
            {
                break;
            }
            pos = pos->next;
        }
    }

    HAL_EXIT_CRITICAL_SECTION();

    return remaining;
}

/* Private functions --------------------------------------------------------*/

/* 硬件定时器 */

/**
 * @brief 初始化硬件定时器
 * @param tick_freq 系统时钟基准频率
 */
static void hw_timer_init(timer_tick_t tick_freq)
{
    if (tick_freq == 0)
    {
        return;
    }
    hal_timer_init(tick_freq);
}

/**
 * @brief 启动硬件定时器
 */
static void hw_timer_start(void)
{
    if (g_timer_hw_active == TIMER_HW_ACTIVE)
    {
        return;
    }

    if (hal_timer_start() == EDF_SUCCESS)
    {
        g_timer_hw_active = TIMER_HW_ACTIVE;
    }
}

/**
 * @brief 停止硬件定时器
 */
static void hw_timer_stop(void)
{
    if (g_timer_hw_active == TIMER_HW_INACTIVE)
    {
        return;
    }

    if (hal_timer_stop() == EDF_SUCCESS)
    {
        g_timer_hw_active = TIMER_HW_INACTIVE;
    }
}

/**
 * @brief 将定时器追加到双向链表尾部（用于IDLE定时器）
 * @param timer 待追加的定时器
 */
static void dlist_push_back(timer_t *timer)
{
    timer_t *tail;

    timer->next = (timer_t*)NULL;

    if (g_timer_list.head == (timer_t*)NULL)
    {
        /* 空链表 */
        timer->prev = (timer_t*)NULL;
        g_timer_list.head = timer;
        return;
    }

    /* 找到尾节点 */
    tail = g_timer_list.head;
    while (tail->next != (timer_t*)NULL)
    {
        tail = tail->next;
    }

    tail->next = timer;
    timer->prev = tail;
}

/**
 * @brief 从双向链表中移除指定定时器（不处理 delta 补偿）
 * @param timer 待移除的定时器
 */
static void dlist_remove(timer_t *timer)
{
    /* 处理前驱节点 */
    if (timer->prev != (timer_t*)NULL)
    {
        timer->prev->next = timer->next;
    }
    else
    {
        /* timer 是头节点 */
        g_timer_list.head = timer->next;
    }

    /* 处理后继节点 */
    if (timer->next != (timer_t*)NULL)
    {
        timer->next->prev = timer->prev;
    }

    timer->next = (timer_t*)NULL;
    timer->prev = (timer_t*)NULL;
}

/**
 * @brief 将定时器按 delta 值插入链表（有序插入，维护 delta 链）
 * @param timer 待插入的定时器（delta_ticks 已设为期望值）
 * @note 插入后自动调整后继节点的 delta 偏移
 */
static void delta_insert(timer_t *timer)
{
    timer_t *pos;
    timer_t *tail;
    timer_tick_t remaining = timer->delta_ticks;

    /* 遍历RUNNING区，找到插入位置（按绝对到期时间递增） */
    pos = g_timer_list.head;
    while (pos != (timer_t*)NULL &&
           pos->timer_status == TIMER_STATUS_RUNNING)
    {
        if (remaining < pos->delta_ticks)
        {
            break;
        }
        remaining -= pos->delta_ticks;
        pos = pos->next;
    }

    /* 在 pos 之前插入 timer */
    timer->delta_ticks = remaining;
    timer->next = pos;

    if (pos != (timer_t*)NULL)
    {
        /* 插入到 pos 之前 */
        timer->prev = pos->prev;
        if (timer->prev != (timer_t*)NULL)
        {
            timer->prev->next = timer;
        }
        else
        {
            /* pos 是头节点，timer 成为新头节点 */
            g_timer_list.head = timer;
        }
        pos->prev = timer;
        /* 调整后继节点的 delta 偏移（仅RUNNING节点需要补偿） */
        if (pos->timer_status == TIMER_STATUS_RUNNING)
        {
            pos->delta_ticks -= remaining;
        }
    }
    else
    {
        /* 插入到链表尾部（pos == NULL，到期时间最晚） */
        tail = g_timer_list.head;
        while (tail != (timer_t*)NULL && tail->next != (timer_t*)NULL)
        {
            tail = tail->next;
        }

        if (tail != (timer_t*)NULL)
        {
            /* 链表非空，接到尾节点后 */
            tail->next = timer;
            timer->prev = tail;
        }
        else
        {
            /* 链表为空，timer 成为头节点 */
            g_timer_list.head = timer;
            timer->prev = (timer_t*)NULL;
        }
    }
}

/**
 * @brief 从 delta 链表中摘除定时器，自动补偿后继节点的 delta
 * @param timer 待摘除的定时器
 */
static void delta_splice_out(timer_t *timer)
{
    /* delta 补偿给后继 RUNNING 节点（IDLE节点不参与delta链） */
    if (timer->next != (timer_t*)NULL &&
        timer->next->timer_status == TIMER_STATUS_RUNNING)
    {
        timer->next->delta_ticks += timer->delta_ticks;
    }

    dlist_remove(timer);
    timer->delta_ticks = 0;
}
