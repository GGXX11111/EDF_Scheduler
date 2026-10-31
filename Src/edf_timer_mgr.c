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
    timer_t * volatile head;    // 链表头指针
    timer_t * volatile tail;    // 链表尾指针
} timer_list_t;

/* Private variables ---------------------------------------------------------*/

/* 硬件定时器激活状态 */
static volatile timer_hw_state_t g_timer_hw_active = TIMER_HW_INACTIVE;

/* 硬件定时器是否已完成初始化 */
static volatile uint8_t g_timer_hw_inited = 0;

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
                    task_event_t notify_event,
                    fp_timer_action_func real_time_action)
{
    /* 参数检查 */
    if (timer == (timer_t*)NULL || 
		timer_reload == 0 || 
		timer->inited != 0)
    {
        return (timer_t*)NULL;
    }

    /* 初始化定时器块 */
    timer->next 			= (timer_t*)NULL;
    timer->prev 			= (timer_t*)NULL;
    timer->timer_mode 		= timer_mode;
    timer->timer_reload 	= timer_reload;
    timer->delta_ticks 		= 0;
    timer->timer_status 	= TIMER_STATUS_IDLE;
    timer->notify_task 		= notify_task;
    timer->notify_event 	= notify_event;
    timer->real_time_action = real_time_action;
    timer->inited 			= 1;

    return timer;
}

/**
 * @brief 启动定时器
 */
edf_status_t timer_start(timer_t *timer)
{
    uint8_t inserted = 0;

    /* 参数检查 */
    if (timer == (timer_t*)NULL || 
		timer->inited == 0)
    {
        return EDF_ERR;
    }

    /* 硬件初始化 */
    if (hw_timer_init(EDF_TICK_FREQ_HZ) != EDF_OK)
    {
        return EDF_ERR;
    }

    HAL_ENTER_CRITICAL_SECTION();

    if (timer->timer_status != TIMER_STATUS_RUNNING)
    {
        /* 设置运行参数并插入 delta 链表 */
        timer->timer_status = TIMER_STATUS_RUNNING;
        timer->delta_ticks = timer->timer_reload;
        delta_insert(timer);
        inserted = 1U;
    }

    /* 确认硬件确实启动 */
    if (g_timer_hw_active == TIMER_HW_INACTIVE)
    {
        if (hw_timer_start() != EDF_OK)
        {
            /* 启动失败：回滚本次插入，避免定时器悬挂在链表中 */
            if (inserted != 0U)
            {
                delta_splice_out(timer);
                timer->timer_status = TIMER_STATUS_IDLE;
            }

            HAL_EXIT_CRITICAL_SECTION();

            return EDF_ERR;
        }
    }

    HAL_EXIT_CRITICAL_SECTION();

    return EDF_OK;
}

/**
 * @brief 停止定时器
 */
edf_status_t timer_stop(timer_t *timer)
{
    edf_status_t ret = EDF_OK;

    /* 参数检查 */
    if (timer == (timer_t*)NULL || 
		timer->inited == 0)
    {
        return EDF_ERR;
    }

    HAL_ENTER_CRITICAL_SECTION();

    if (timer->timer_status == TIMER_STATUS_RUNNING)
    {
        /* 从链表中摘除定时器（自动补偿后继delta） */
        delta_splice_out(timer);

        timer->timer_status = TIMER_STATUS_STOPPED;
        timer->delta_ticks = 0;

        if (g_timer_list.head == (timer_t*)NULL)
        {
            ret = hw_timer_stop();
        }
    }
    else if (timer->timer_status == TIMER_STATUS_EXPIRED)
    {
        /* 显式停止已到期定时器，统一为 STOPPED */
        timer->timer_status = TIMER_STATUS_STOPPED;
        timer->delta_ticks = 0;
    }
	else
	{
		/* timer->timer_status == TIMER_STATUS_IDLE 
		   timer->timer_status == TIMER_STATUS_STOPPED
		   无需处理 */
	}

    HAL_EXIT_CRITICAL_SECTION();

    return ret;
}

/**
 * @brief 修改定时器模式
 */
edf_status_t timer_set_mode(timer_t *timer, timer_mode_t timer_mode)
{
    /* 参数检查 */
    if (timer == (timer_t*)NULL || 
		timer->inited == 0 || 
    	(timer_mode != TIMER_MODE_SINGLE && 
		timer_mode != TIMER_MODE_PERIODIC))
    {
        return EDF_ERR;
    }

    HAL_ENTER_CRITICAL_SECTION();

    timer->timer_mode = timer_mode;

    HAL_EXIT_CRITICAL_SECTION();

    return EDF_OK;
}

/**
 * @brief 修改定时器重载值
 */
edf_status_t timer_set_reload(timer_t *timer, timer_tick_t timer_reload)
{
    /* 参数检查 */
    if (timer == (timer_t*)NULL || 
		timer->inited == 0 || 
		timer_reload == 0U)
    {
        return EDF_ERR;
    }

    HAL_ENTER_CRITICAL_SECTION();

    timer->timer_reload = timer_reload;

    /* 运行中：摘除后按新周期重新插入，旧周期的剩余节拍作废 */
    if (timer->timer_status == TIMER_STATUS_RUNNING)
    {
        delta_splice_out(timer);
        timer->delta_ticks = timer_reload;
        delta_insert(timer);
    }

    HAL_EXIT_CRITICAL_SECTION();

    return EDF_OK;
}

/**
 * @brief 定时器更新函数，应该在系统时钟中断服务程序中调用
 *
 * 处理流程：
 *   1. 递减头节点 delta_ticks
 *   2. 处理所有 delta_ticks==0 的到期定时器
 *   3. 无活动定时器时停止硬件节拍
 */
void timer_update(void)
{
    timer_t *head;

    HAL_ENTER_CRITICAL_SECTION();

    if (g_timer_list.head != (timer_t*)NULL)
    {
        /* 1. 递减头节点 delta */
        if (g_timer_list.head->delta_ticks > 0U)
        {
            g_timer_list.head->delta_ticks--;
        }

        /* 2. 处理所有在同一节拍到期的定时器 */
        while (g_timer_list.head != (timer_t*)NULL &&
               g_timer_list.head->timer_status == TIMER_STATUS_RUNNING &&
               g_timer_list.head->delta_ticks == 0)
        {
            head = g_timer_list.head;

            if ((head->timer_mode == TIMER_MODE_PERIODIC) &&
                (head->timer_reload > 0U))
            {
                /* 周期定时器：摘除后重新插入 */
                delta_splice_out(head);
                head->timer_status = TIMER_STATUS_RUNNING;
                head->delta_ticks = head->timer_reload;
                delta_insert(head);
            }
            else
            {
                /* 单次定时器，或周期定时器 reload 为 0（异常情况）：
            	   摘除并置 EXPIRED，避免 delta_ticks 恒为 0 导致死循环 */
                delta_splice_out(head);
                head->timer_status = TIMER_STATUS_EXPIRED;
            }

            /* 实时动作，在中断上下文执行，必须简短 */
            if (head->real_time_action != (fp_timer_action_func)NULL)
            {
                head->real_time_action(head);
            }

            /* 触发超时事件，延迟执行 */
            task_set_event(head->notify_task, head->notify_event);
        }
    }

    /* 3. 无活动定时器则停止硬件，避免 ISR 空转 */
    if ((g_timer_list.head == (timer_t*)NULL) &&
        (g_timer_hw_active == TIMER_HW_ACTIVE))
    {
        hw_timer_stop();
    }

    HAL_EXIT_CRITICAL_SECTION();
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

    if (g_timer_hw_inited != 0U)
    {
        return EDF_OK;
    }

    if (hal_timer_init(tick_freq) != EDF_OK)
    {
        return EDF_ERR;
    }

    g_timer_hw_inited = 1U;

    return EDF_OK;
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
 * @brief 将定时器追加到双向链表尾部
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
