/**
 ******************************************************************************
 * @file    edf_task_mgr.c
 * @brief   任务管理器实现
 ******************************************************************************
 */

/* Includes ------------------------------------------------------------------*/

#include "edf_task_mgr.h"
#include "edf_base.h"

/* Private constants ---------------------------------------------------------*/

/* Private types -------------------------------------------------------------*/

/* 优先级就绪子队列定义 */
typedef struct priority_ready_queue
{
    task_t * volatile head;     // 子队列头指针
    task_t * volatile tail;     // 子队列尾指针
} priority_ready_queue_t;

/* Private variables ---------------------------------------------------------*/

/* 优先级位图：bit[i]=1 表示优先级i有就绪任务 */
static volatile uint8_t g_priority_bitmap;

/* 每个优先级一个就绪子队列 */
#define TASK_PRIORITY_MAX_COUNT		(TASK_PRIORITY_LOWEST + 1)	// 优先级数量
static priority_ready_queue_t g_ready_table[TASK_PRIORITY_MAX_COUNT] = {0};

/* 当前运行任务指针 */
static task_t * volatile g_current_task = (task_t*)NULL;

/* Private defines -----------------------------------------------------------*/

/* Private function prototypes ----------------------------------------------*/

/* 静态函数声明 */
static task_t *get_next_active_task(void);
static edf_status_t add_to_ready_list(task_t *task);
static edf_status_t remove_from_ready_list(task_t *task);

/* Exported constants --------------------------------------------------------*/

/* Exported variables --------------------------------------------------------*/

/* Exported functions --------------------------------------------------------*/

/**
 * @brief 初始化任务（静态创建）
 */
task_t *task_init(task_t *task,
                  fp_task_init_func init_func,
                  fp_task_handler_func handler_func,
                  task_priority_t priority,
                  void *task_data)
{
    /* 参数检查 */
    if (task == (task_t*)NULL || 
        handler_func == (fp_task_handler_func)NULL || 
        priority < TASK_PRIORITY_HIGHEST || 
        priority > TASK_PRIORITY_LOWEST || 
        task->inited != 0)
    {
        return (task_t*)NULL;
    }

    /* 初始化任务控制块 */
    task->next_ready 		= (task_t*)NULL;
    task->task_priority 	= priority;
    task->in_ready_list 	= 0;
    task->task_event 		= EVENT_NULL;
    task->task_handler_func = handler_func;
    task->task_init_func 	= init_func;
    task->task_data 		= task_data;
    task->inited 			= 1;

    /* 创建后立即调用初始化函数 */
    if (init_func != (fp_task_init_func)NULL)
    {
        init_func(task, task_data);
    }

    return task;
}

/**
 * @brief 设置任务事件
 */
edf_status_t task_set_event(task_t *task, task_event_t event)
{
    /* 参数检查 */
    if (task == (task_t*)NULL || 
		task->inited == 0)
    {
        return EDF_ERR;
    }

    if (event == EVENT_NULL)
    {
        return EDF_OK;
    }

    HAL_ENTER_CRITICAL_SECTION();

    /* 设置任务事件标志 */
    task->task_event |= event;

    /* 任务此前不在就绪队列中（且不是当前运行任务），则加入就绪队列 */
    if ((task->in_ready_list == 0) && 
		(task->task_event != EVENT_NULL) && 
		(task != g_current_task))
    {
        add_to_ready_list(task);
    }

    HAL_EXIT_CRITICAL_SECTION();

    return EDF_OK;
}

/**
 * @brief 清除任务事件
 */
edf_status_t task_clear_event(task_t *task, task_event_t event)
{
    /* 参数检查 */
    if (task == (task_t*)NULL || 
		task->inited == 0)
    {
        return EDF_ERR;
    }

    if (event == EVENT_NULL)
    {
        return EDF_OK;
    }

    HAL_ENTER_CRITICAL_SECTION();

	/* 清除任务事件标志 */
    task->task_event &= ~event;

	/* 任务已在就绪队列中且事件已全部清除，则从就绪队列中移除 */
    if ((task->in_ready_list != 0) && 
		(task->task_event == EVENT_NULL) && 
		(task != g_current_task))
    {
        remove_from_ready_list(task);
    }

    HAL_EXIT_CRITICAL_SECTION();

    return EDF_OK;
}

/**
 * @brief 启动任务调度器，进入调度循环
 * @note 此函数永不返回
 */
void task_scheduler_start(void)
{
    task_t *current_task;
    task_event_t task_event;
    task_event_t ret_event;

    while (1)
    {
        task_event = EVENT_NULL;

        HAL_ENTER_CRITICAL_SECTION();

        current_task = get_next_active_task();

        if (current_task != (task_t*)NULL)
        {
            task_event = current_task->task_event;
            current_task->task_event = EVENT_NULL;
        }

        if (task_event != EVENT_NULL)
        {
            g_current_task = current_task;
        }

        HAL_EXIT_CRITICAL_SECTION();

        if (task_event != EVENT_NULL)
        {
            ret_event = current_task->task_handler_func(current_task,
                                                        task_event,
                                                        current_task->task_data);

            HAL_ENTER_CRITICAL_SECTION();

            g_current_task = (task_t*)NULL;
            current_task->task_event |= ret_event;

            if (current_task->task_event != EVENT_NULL)
            {
                add_to_ready_list(current_task);
            }

            HAL_EXIT_CRITICAL_SECTION();
        }
        else
        {
            EDF_IDLE_HOOK();
        }
    }
}

/* Private functions --------------------------------------------------------*/

/**
 * @brief 查找下一个有事件的任务控制块
 * @return 找到返回任务控制块指针，未找到返回NULL
 * @note 从优先级位图找最高优先级，再从该优先级子队列头部获取
 */
static task_t *get_next_active_task(void)
{
    task_t *active_task;
    uint8_t highest_prio;
    priority_ready_queue_t *queue;

    /* 位图为空，无就绪任务 */
    if (g_priority_bitmap == 0)
    {
        return (task_t*)NULL;
    }

    /* 从位图找最高优先级（数值最小 = 优先级最高，即最低有效位） */
    {
        uint8_t bmp = g_priority_bitmap;
        highest_prio = 0;
        while ((bmp & 1U) == 0)
        {
            bmp >>= 1;
            highest_prio++;
        }
    }

    /* 从该优先级子队列头部取任务 */
    queue = &g_ready_table[highest_prio];
    active_task = queue->head;

    if (active_task == (task_t*)NULL)
    {
        /* 位图与子队列不一致：清位，避免调度器空转 */
        g_priority_bitmap &= ~(1U << highest_prio);

        return (task_t*)NULL;
    }

    queue->head = active_task->next_ready;
    if (queue->head == (task_t*)NULL)
    {
        queue->tail = (task_t*)NULL;
        g_priority_bitmap &= ~(1U << highest_prio);
    }

    active_task->next_ready = (task_t*)NULL;
    active_task->in_ready_list = 0;

    return active_task;
}

/**
 * @brief 将任务添加到对应优先级子队列尾部
 * @param task 任务控制块指针
 * @return 成功入队（或已在队列中）返回EDF_OK，否则返回EDF_ERR
 * @note 此函数必须在临界区内调用
 */
static edf_status_t add_to_ready_list(task_t *task)
{
    task_priority_t prio = task->task_priority;
    priority_ready_queue_t *queue;

    /* 已在子队列中则直接返回 */
    if (task->in_ready_list != 0)
    {
        return EDF_OK;
    }

    queue = &g_ready_table[prio];
    task->next_ready = (task_t*)NULL;

    /* 插入子队列尾部 */
    if (queue->head == (task_t*)NULL)
    {
        queue->head = task;
        queue->tail = task;
    }
    else
    {
        queue->tail->next_ready = task;
        queue->tail = task;
    }

    task->in_ready_list = 1;

    /* 置位位图 */
    g_priority_bitmap |= (1U << prio);

    return EDF_OK;
}

/**
 * @brief 从对应优先级子队列中移除任务
 * @param task 任务控制块指针
 * @return 成功移除（或不在队列中）返回EDF_OK，否则返回EDF_ERR
 * @note 此函数必须在临界区内调用
 */
static edf_status_t remove_from_ready_list(task_t *task)
{
    task_priority_t prio = task->task_priority;
    priority_ready_queue_t *queue;
    task_t *curr;
    task_t *prev;

    /* 本就不在子队列中则直接返回 */
    if (task->in_ready_list == 0)
    {
        return EDF_OK;
    }

    queue = &g_ready_table[prio];
    prev = (task_t*)NULL;
    curr = queue->head;

    while (curr != (task_t*)NULL)
    {
        if (curr == task)
        {
            if (prev == (task_t*)NULL)
            {
                queue->head = curr->next_ready;
            }
            else
            {
                prev->next_ready = curr->next_ready;
            }

            /* 摘的是尾节点（含唯一节点）时回退尾指针 */
            if (curr == queue->tail)
            {
                queue->tail = prev;
            }

            curr->next_ready = (task_t*)NULL;
            curr->in_ready_list = 0;

            /* 子队列空了，清除位图对应位 */
            if (queue->head == (task_t*)NULL)
            {
                g_priority_bitmap &= ~(1U << prio);
            }

            return EDF_OK;
        }

        prev = curr;
        curr = curr->next_ready;
    }

    /* 链表与标志不一致：清除标志，允许后续重新入队 */
    task->in_ready_list = 0;

    return EDF_ERR;
}