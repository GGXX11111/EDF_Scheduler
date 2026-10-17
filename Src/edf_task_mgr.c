/**
 ******************************************************************************
 * @file    edf_task_mgr.c
 * @brief   任务管理器实现
 ******************************************************************************
 */

/* Includes ------------------------------------------------------------------*/

#include "edf_task_mgr.h"
#include "edf_base.h"
#include "edf_mem_mgr.h"

/* Private constants ---------------------------------------------------------*/

/* Private types -------------------------------------------------------------*/

/* 任务控制块定义 */
struct task_ctrl_blk
{
    struct task_ctrl_blk *next_ready;       // 就绪队列下一个节点
    task_priority_t task_priority;          // 任务优先级
    task_status_t task_status;              // 任务状态
	uint8_t reserved[2];
    task_event_t task_event;                // 任务事件标志
    fp_task_init_func task_init_func;       // 初始化函数指针
    fp_task_handler_func task_handler_func; // 事件处理函数指针
    void *task_data;                        // 任务相关数据指针
};

/* 优先级就绪子队列定义 */
typedef struct priority_ready_queue
{
    task_t *head;                       // 子队列头指针
    task_t *tail;                       // 子队列尾指针
    uint8_t task_count;                 // 子队列任务数量
} priority_ready_queue_t;

/* Private variables ---------------------------------------------------------*/

/* 优先级位图：bit[i]=1 表示优先级i有就绪任务 */
static uint8_t g_priority_bitmap;

/* 每个优先级一个就绪子队列 */
static priority_ready_queue_t g_ready_table[TASK_PRIORITY_COUNT] = {0};

/* 当前运行任务指针 */
static task_t *g_current_task = (task_t*)NULL;

/* Private defines -----------------------------------------------------------*/

/* Private function prototypes ----------------------------------------------*/

/* 静态函数声明 */
static task_t *get_next_active_task(void);
static void add_to_ready_list(task_t *task);
static void remove_from_ready_list(task_t *task);

/* Exported constants --------------------------------------------------------*/

/* Exported variables --------------------------------------------------------*/

/* Exported functions --------------------------------------------------------*/

/**
 * @brief 创建任务
 */
task_t *task_create(fp_task_init_func init_func,
                    fp_task_handler_func handler_func,
                    task_priority_t priority,
                    void *task_data)
{
    task_t *new_task;

    /* 参数检查 */
    if (handler_func == (fp_task_handler_func)NULL)
    {
        return (task_t*)NULL;
    }

    if (priority > TASK_PRIORITY_LOWEST)
    {
        return (task_t*)NULL;
    }

    /* 创建新的任务控制块 */
    new_task = (task_t*)mem_malloc(sizeof(task_t));
    if (new_task == (task_t*)NULL)
    {
        return (task_t*)NULL;
    }

    /* 初始化新的任务控制块 */
    new_task->next_ready = (task_t*)NULL;
    new_task->task_priority = priority;
    new_task->task_status = TASK_STATUS_BLOCKED;
    new_task->task_event = EVENT_NULL;
    new_task->task_handler_func = handler_func;
    new_task->task_init_func = init_func;
    new_task->task_data = task_data;

    /* 创建后立即调用初始化函数 */
    if (init_func != (fp_task_init_func)NULL)
    {
        init_func(new_task, task_data);
    }

    return new_task;
}

/**
 * @brief 删除任务
 */
void task_delete(task_t *task)
{
    /* 参数检查 */
    if (task == (task_t*)NULL)
    {
        return;
    }

    HAL_ENTER_CRITICAL_SECTION();

    /* 检查任务是否正在运行 */
    if (task == g_current_task)
    {
        HAL_EXIT_CRITICAL_SECTION();

        return;
    }

    remove_from_ready_list(task);

    HAL_EXIT_CRITICAL_SECTION();

    mem_free(task);
}

/**
 * @brief 设置任务事件
 */
uint8_t task_set_event(task_t *task, task_event_t event)
{
    task_event_t old_event;

    /* 参数检查 */
    if (task == (task_t*)NULL)
    {
        return EDF_FAILURE;
    }

    if (event == EVENT_NULL)
    {
        return EDF_SUCCESS;
    }

    HAL_ENTER_CRITICAL_SECTION();

    /* 保存原有事件状态 */
    old_event = task->task_event;

    /* 设置任务事件标志 */
    task->task_event |= event;

    /* 如果之前没有事件，现在有事件了，则加入就绪队列 */
    if ((old_event == EVENT_NULL) &&
        (task->task_event != EVENT_NULL) &&
        (task != g_current_task))
    {
        task->task_status = TASK_STATUS_READY;
        add_to_ready_list(task);
    }

    HAL_EXIT_CRITICAL_SECTION();

    return EDF_SUCCESS;
}

/**
 * @brief 清除任务事件
 */
uint8_t task_clear_event(task_t *task, task_event_t event)
{
    /* 参数检查 */
    if (task == (task_t*)NULL)
    {
        return EDF_FAILURE;
    }

    if (event == EVENT_NULL)
    {
        return EDF_SUCCESS;
    }

    HAL_ENTER_CRITICAL_SECTION();

    if (task != g_current_task)
    {
        task->task_event &= ~event;

        if (task->task_event == EVENT_NULL)
        {
            task->task_status = TASK_STATUS_BLOCKED;
            remove_from_ready_list(task);
        }
    }

    HAL_EXIT_CRITICAL_SECTION();

    return EDF_SUCCESS;
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
            current_task->task_event = 0;
        }

        if (task_event != EVENT_NULL)
        {
            g_current_task = current_task;
            current_task->task_status = TASK_STATUS_RUNNING;
        }

        HAL_EXIT_CRITICAL_SECTION();

        if (task_event != EVENT_NULL)
        {

            /* 调用任务事件处理函数，获取返回的事件标志 */
            if (current_task->task_handler_func != (fp_task_handler_func)NULL)
            {
                ret_event = current_task->task_handler_func(current_task,
                                                            task_event,
                                                            current_task->task_data);
            }
            else
            {
                ret_event = EVENT_NULL;
            }

            HAL_ENTER_CRITICAL_SECTION();

            g_current_task = (task_t*)NULL;
            current_task->task_event |= ret_event;

            if (current_task->task_event != EVENT_NULL)
            {
                current_task->task_status = TASK_STATUS_READY;
                add_to_ready_list(current_task);
            }
            else
            {
                current_task->task_status = TASK_STATUS_BLOCKED;
            }

            HAL_EXIT_CRITICAL_SECTION();
        }
        else
        {
            HAL_WFI();
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
    int8_t highest_prio;
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

    if (active_task != (task_t*)NULL)
    {
        queue->head = active_task->next_ready;
        if (queue->head == (task_t*)NULL)
        {
            queue->tail = (task_t*)NULL;
        }
        queue->task_count--;
        active_task->next_ready = (task_t*)NULL;

        /* 子队列空了，清除位图对应位 */
        if (queue->task_count == 0)
        {
            g_priority_bitmap &= ~(1U << highest_prio);
        }
    }

    return active_task;
}

/**
 * @brief 获取当前正在运行的任务句柄
 * @return 当前任务句柄，未启动或无任务运行时返回NULL
 */
task_t *task_get_current(void)
{
    return g_current_task;
}

/**
 * @brief 将任务添加到对应优先级子队列尾部
 * @param task 任务控制块指针
 * @note 此函数必须在临界区内调用
 * @note 同优先级任务按FIFO顺序排列，自然实现Round-Robin轮转
 */
static void add_to_ready_list(task_t *task)
{
    uint8_t prio = task->task_priority;
    priority_ready_queue_t *queue = &g_ready_table[prio];
    task_t *curr;

    /* 检查是否已在子队列中（防重复添加） */
    curr = queue->head;
    while (curr != (task_t*)NULL)
    {
        if (curr == task)
        {
            return;
        }
        curr = curr->next_ready;
    }

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
    queue->task_count++;

    /* 置位位图 */
    g_priority_bitmap |= (1U << prio);
}

/**
 * @brief 从对应优先级子队列中移除任务
 * @param task 任务控制块指针
 * @note 此函数必须在临界区内调用
 */
static void remove_from_ready_list(task_t *task)
{
    uint8_t prio = task->task_priority;
    priority_ready_queue_t *queue = &g_ready_table[prio];
    task_t *curr;
    task_t *prev;

    prev = (task_t*)NULL;
    curr = queue->head;

    while (curr != (task_t*)NULL)
    {
        if (curr == task)
        {
            if (prev == (task_t*)NULL)
            {
                queue->head = curr->next_ready;
                if (queue->head == (task_t*)NULL)
                {
                    queue->tail = (task_t*)NULL;
                }
            }
            else
            {
                prev->next_ready = curr->next_ready;
                if (curr == queue->tail)
                {
                    queue->tail = prev;
                }
            }
            curr->next_ready = (task_t*)NULL;
            queue->task_count--;

            /* 子队列空了，清除位图对应位 */
            if (queue->task_count == 0)
            {
                g_priority_bitmap &= ~(1U << prio);
            }

            return;
        }
        prev = curr;
        curr = curr->next_ready;
    }
}