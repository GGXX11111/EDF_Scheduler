/**
 ******************************************************************************
 * @file    edf_timer_mgr.c
 * @brief   定时器管理器实现
 ******************************************************************************
 */

/* Includes ------------------------------------------------------------------*/

#include "edf_timer_mgr.h"
#include "edf_base.h"

/* Private constants ---------------------------------------------------------*/

/* Private types -------------------------------------------------------------*/

/* 软件定时器双向链表 */
typedef struct timer_list
{
    timer_t *head;                  // 链表头指针（NULL表示空链表）
    timer_t *tail;                  // 链表尾指针（NULL表示空链表）
} timer_list_t;

/* Private variables ---------------------------------------------------------*/

/* 硬件定时器激活状态 */
static timer_hw_state_t g_timer_hw_active = TIMER_HW_INACTIVE;

/* 全局软件定时器双向链表 */
static timer_list_t g_timer_list = {0};

/* Private defines -----------------------------------------------------------*/

/* Private function prototypes ----------------------------------------------*/

/* 静态函数声明 */
static void dlist_remove(timer_t *timer);
static void dlist_push_back(timer_t *timer);
static void delta_insert(timer_t *timer);
static void delta_splice_out(timer_t *timer);
static edf_status_t hw_timer_init(timer_tick_t tick_freq);
static edf_status_t hw_timer_start(void);
static edf_status_t hw_timer_stop(void);

/* Exported constants --------------------------------------------------------*/

/* Exported variables --------------------------------------------------------*/

/* Exported functions --------------------------------------------------------*/

/* 软件定时器管理 */

/**
 * @brief 初始化定时器（静态创建）
 */
timer_t *timer_init(timer_t *timer,
                    timer_mode_t timer_mode,
                    timer_tick_t timer_reload,
                    task_t *notify_task,
                    task_event_t notify_event)
{
    /* 参数检查 */
    if (timer == (timer_t*)NULL || timer_reload == 0)
    {
        return (timer_t*)NULL;
    }

    /* 已初始化的控制块不允许重复初始化，避免覆盖链表链接 */
    if (timer->inited != 0)
    {
        return (timer_t*)NULL;
    }

    /* 初始化新定时器块（IDLE状态，追加到链表尾部） */
    timer->next 		= (timer_t*)NULL;
    timer->prev 		= (timer_t*)NULL;
    timer->timer_mode 	= timer_mode;
    timer->timer_reload = timer_reload;
    timer->delta_ticks 	= 0;
    timer->timer_status = TIMER_STATUS_IDLE;
    timer->notify_task 	= notify_task;
    timer->notify_event = notify_event;
    timer->inited 		= 1;

	HAL_ENTER_CRITICAL_SECTION();

    /* 追加到链表尾部 */
    dlist_push_back(timer);

    HAL_EXIT_CRITICAL_SECTION();

    return timer;
}

/**
 * @brief 启动定时器
 */
edf_status_t timer_start(timer_t *timer)
{
    /* 参数检查 */
    if (timer == (timer_t*)NULL || timer->inited == 0)
    {
        return EDF_ERR;
    }

    HAL_ENTER_CRITICAL_SECTION();

    /* 首次启动软件定时器时，初始化并启动硬件节拍定时器 */
    if (g_timer_hw_active == TIMER_HW_INACTIVE)
    {
        if ((hw_timer_init(EDF_TICK_FREQ_HZ) != EDF_OK) ||
            (hw_timer_start() != EDF_OK))
        {
            HAL_EXIT_CRITICAL_SECTION();

            return EDF_ERR;
        }
    }

    /* 如果已经在运行，则不再重新启动 */
    if (timer->timer_status == TIMER_STATUS_RUNNING)
    {
        HAL_EXIT_CRITICAL_SECTION();

        return EDF_OK;
    }

    /* 非运行状态：从链表尾部摘除 */
    dlist_remove(timer);

    /* 设置运行参数并插入 delta 链表 */
    timer->timer_status = TIMER_STATUS_RUNNING;
    timer->delta_ticks = timer->timer_reload;
    delta_insert(timer);

    HAL_EXIT_CRITICAL_SECTION();

    return EDF_OK;
}

/**
 * @brief 停止定时器
 */
edf_status_t timer_stop(timer_t *timer)
{
    edf_status_t status = EDF_OK;

    /* 参数检查 */
    if (timer == (timer_t*)NULL || timer->inited == 0)
    {
        return EDF_ERR;
    }

    HAL_ENTER_CRITICAL_SECTION();

    /* 如果已经停止（非RUNNING），则不再重复停止 */
    if (timer->timer_status != TIMER_STATUS_RUNNING)
    {
        HAL_EXIT_CRITICAL_SECTION();

        return EDF_OK;
    }

    /* 摘除（自动补偿后继delta） */
    delta_splice_out(timer);

    /* 置为STOPPED状态，追加到链表尾部 */
    timer->timer_status = TIMER_STATUS_STOPPED;
    timer->delta_ticks = 0;
    dlist_push_back(timer);

    /* 若无剩余 RUNNING 定时器，停止硬件定时器（RUNNING 节点一定在表头） */
    if ((g_timer_list.head == (timer_t*)NULL ||
         g_timer_list.head->timer_status != TIMER_STATUS_RUNNING) &&
        (g_timer_hw_active == TIMER_HW_ACTIVE))
    {
        status = hw_timer_stop();
    }

    HAL_EXIT_CRITICAL_SECTION();

    return status;
}

/**
 * @brief 定时器更新函数，应该在系统时钟中断服务程序中调用
 *
 * 处理流程：
 *   1. 若链表为空或头节点非 RUNNING，快速返回
 *   2. 递减头节点 delta_ticks
 *   3. 处理所有 delta_ticks==0 的到期定时器
 */
void timer_update(void)
{
    timer_t *head;

    /* 1. 无 RUNNING 定时器（链表空或头节点为IDLE），快速返回 */
    if (g_timer_list.head == (timer_t*)NULL ||
        g_timer_list.head->timer_status != TIMER_STATUS_RUNNING)
    {
        return;
    }
	
    /* 2. 递减头节点 delta（仅操作链表头，O(1)） */
    if (g_timer_list.head->delta_ticks > 0U)
    {
        g_timer_list.head->delta_ticks--;
    }

    /* 3. 处理所有在同一节拍到期的定时器（delta_ticks == 0） */
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
            /* 单次定时器：摘除，置EXPIRED，追加到链表尾部 */
            delta_splice_out(head);
            head->timer_status = TIMER_STATUS_EXPIRED;
            dlist_push_back(head);
        }
    }
}

/* Private functions --------------------------------------------------------*/

/* 硬件定时器 */

/**
 * @brief 初始化硬件定时器
 * @param tick_freq 系统时钟基准频率
 */
static edf_status_t hw_timer_init(timer_tick_t tick_freq)
{
    if (tick_freq == 0)
    {
        return EDF_ERR;
    }

    return hal_timer_init(tick_freq);
}

/**
 * @brief 启动硬件定时器
 */
static edf_status_t hw_timer_start(void)
{
    if (g_timer_hw_active == TIMER_HW_ACTIVE)
    {
        return EDF_OK;
    }

    if (hal_timer_start() != EDF_OK)
    {
        return EDF_ERR;
    }

    g_timer_hw_active = TIMER_HW_ACTIVE;

    return EDF_OK;
}

/**
 * @brief 停止硬件定时器
 */
static edf_status_t hw_timer_stop(void)
{
    if (g_timer_hw_active == TIMER_HW_INACTIVE)
    {
        return EDF_OK;
    }

    if (hal_timer_stop() != EDF_OK)
    {
        return EDF_ERR;
    }

    g_timer_hw_active = TIMER_HW_INACTIVE;

    return EDF_OK;
}

/**
 * @brief 将定时器追加到双向链表尾部（用于非运行状态的定时器）
 * @param timer 待追加的定时器
 */
static void dlist_push_back(timer_t *timer)
{
    timer->next = (timer_t*)NULL;

    if (g_timer_list.head == (timer_t*)NULL)
    {
        /* 空链表 */
        timer->prev = (timer_t*)NULL;
        g_timer_list.head = timer;
    }
    else
    {
        /* 接到尾节点后 */
        g_timer_list.tail->next = timer;
        timer->prev = g_timer_list.tail;
    }

    g_timer_list.tail = timer;
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
    else
    {
        /* timer 是尾节点 */
        g_timer_list.tail = timer->prev;
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
        dlist_push_back(timer);
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
