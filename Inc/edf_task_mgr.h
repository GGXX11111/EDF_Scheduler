/**
 ******************************************************************************
 * @file    edf_task_mgr.h
 * @brief   任务管理器接口
 ******************************************************************************
 */

#ifndef EDF_TASK_MGR_H
#define EDF_TASK_MGR_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/

#include "edf_base.h"

/* Exported constants --------------------------------------------------------*/

/* 任务事件标志 */
#define EVENT_NULL              0x00000000
#define EVENT_FULL              0xFFFFFFFF

/* Exported types ------------------------------------------------------------*/

/* 任务事件类型 */
typedef uint32_t                task_event_t;       // 任务事件类型

/* 任务优先级（数值越小优先级越高） */
typedef enum task_priority
{
    TASK_PRIORITY_HIGHEST   = 0,                            // 最高优先级
    TASK_PRIORITY_HIGH      = 1,                            // 高优先级
    TASK_PRIORITY_NORMAL    = 2,                            // 正常优先级
    TASK_PRIORITY_LOW       = 3,                            // 低优先级
    TASK_PRIORITY_LOWEST    = 4,                            // 最低优先级
    TASK_PRIORITY_MAX_COUNT = (TASK_PRIORITY_LOWEST + 1),   // 优先级数量
} task_priority_t;

/* 任务句柄类型 */
typedef struct task_ctrl_blk task_t;

/**
 * @brief 任务初始化函数
 * @param task      任务句柄
 * @param task_data 任务相关数据指针
 * @note 在 task_init() 时自动执行，可为 NULL
 */
typedef void (*fp_task_init_func)(task_t *task, void *task_data);

/**
 * @brief 任务事件处理函数
 * @param task       任务句柄
 * @param task_event 待处理的事件标志（可组合）
 * @param task_data  任务相关数据指针
 * @return 剩余未处理的事件标志
 */
typedef task_event_t (*fp_task_handler_func)(task_t *task,
                                             task_event_t task_event,
                                             void *task_data);

/**
 * @brief 任务控制块
 */
struct task_ctrl_blk
{
    struct task_ctrl_blk *next_ready;       // 就绪队列下一个节点
    task_priority_t task_priority;          // 任务优先级
    volatile uint8_t inited;                // 初始化标志：0-未初始化，1-已初始化
    uint8_t in_ready_list;                  // 就绪队列成员标志：1-已在队列中
    uint8_t reserved0[2];
    volatile task_event_t task_event;       // 任务事件标志
    fp_task_init_func task_init_func;       // 初始化函数指针
    fp_task_handler_func task_handler_func; // 事件处理函数指针
    void *task_data;                        // 任务相关数据指针
};

/* Exported defines ----------------------------------------------------------*/

/**
 * @brief 检查事件标志是否置位
 * @param event 事件变量
 * @param flag  要检查的事件标志
 * @return 事件已设置返回非零，否则返回0
 */
#define TASK_EVENT_CHECK(event, flag)   ((event) & (flag))

/**
 * @brief 清除事件标志
 * @param event 事件变量
 * @param flag  要清除的事件标志
 * @return 清除后的新值
 */
#define TASK_EVENT_CLEAR(event, flag)   ((event) &= ~(flag))

/**
 * @brief 设置事件标志
 * @param event 事件变量
 * @param flag  要设置的事件标志
 * @return 设置后的新值
 */
#define TASK_EVENT_SET(event, flag)     ((event) |= (flag))

/* Exported functions --------------------------------------------------------*/

/******************************************************************************/
/*                        任务管理函数                                        */
/******************************************************************************/

/**
 * @brief 初始化任务（静态创建）
 * @param task         调用者提供的任务控制块（全局或静态变量）
 * @param init_func    任务初始化函数（可NULL）
 * @param handler_func 任务事件处理函数（不可NULL）
 * @param priority     任务优先级
 * @param task_data    任务数据指针（可NULL）
 * @return 成功返回 task，参数无效返回NULL
 * @note 初始化后立即调用 init_func(task, task_data)
 * @note 同一个 task 只能初始化一次，重复 init 会返回 NULL
 * @note 初始化后长期存在，没有对应的销毁接口；
 * @note 禁止在中断上下文调用
 */
task_t *task_init(task_t *task,
                  fp_task_init_func init_func,
                  fp_task_handler_func handler_func,
                  task_priority_t priority,
                  void *task_data);

/**
 * @brief 设置任务事件
 * @param task  任务句柄
 * @param event 待设置的事件标志（可组合）
 * @return 成功返回EDF_OK，失败返回EDF_ERR
 * @note 允许在中断上下文调用
 */
edf_status_t task_set_event(task_t *task, task_event_t event);

/**
 * @brief 清除任务事件
 * @param task  任务句柄
 * @param event 待清除的事件标志（可组合）
 * @return 成功返回EDF_OK，失败返回EDF_ERR
 * @note 允许在中断上下文调用
 */
edf_status_t task_clear_event(task_t *task, task_event_t event);

/**
 * @brief 启动任务调度器，进入调度循环
 * @note 此函数永不返回
 */
void task_scheduler_start(void);

#ifdef __cplusplus
}
#endif

#endif /* EDF_TASK_MGR_H */
