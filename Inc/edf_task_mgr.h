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

/* Exported types ------------------------------------------------------------*/

/* 任务句柄类型 */
typedef struct task_ctrl_blk task_t;

/**
 * @brief 任务初始化函数
 * @param task      任务句柄
 * @param task_data 任务相关数据指针
 * @note 在 task_create() 时自动执行，可为 NULL
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
 * @brief 创建任务
 * @param init_func    任务初始化函数（可NULL）
 * @param handler_func 任务事件处理函数（不可NULL）
 * @param priority     任务优先级
 * @param task_data    任务数据指针（可NULL）
 * @return 成功返回任务句柄，失败返回NULL
 * @note 创建后立即调用 init_func(task, task_data)
 * @note 禁止在中断上下文调用
 */
task_t *task_create(fp_task_init_func init_func,
                    fp_task_handler_func handler_func,
                    task_priority_t priority,
                    void *task_data);

/**
 * @brief 删除任务并释放内存
 * @param task 任务句柄
 * @note 不应删除还在被使用的句柄
 * @note 不可重复删除，删除后需将句柄置 NULL
 * @note 禁止在中断上下文调用
 */
void task_delete(task_t *task);

/**
 * @brief 设置任务事件
 * @param task  任务句柄
 * @param event 待设置的事件标志（可组合）
 * @return 成功返回EDF_SUCCESS，失败返回EDF_FAILURE
 * @note 允许在中断上下文调用
 */
uint8_t task_set_event(task_t *task, task_event_t event);

/**
 * @brief 清除任务事件
 * @param task  任务句柄
 * @param event 待清除的事件标志（可组合）
 * @return 成功返回EDF_SUCCESS，失败返回EDF_FAILURE
 * @note 允许在中断上下文调用
 */
uint8_t task_clear_event(task_t *task, task_event_t event);

/**
 * @brief 获取当前正在运行的任务句柄
 * @return 当前任务句柄，未启动或无任务运行时返回NULL
 * @note 不应在中断上下文调用，属于无意义操作
 */
task_t *task_get_current(void);

/**
 * @brief 启动任务调度器，进入调度循环
 * @note 此函数永不返回
 */
void task_scheduler_start(void);

#ifdef __cplusplus
}
#endif

#endif /* EDF_TASK_MGR_H */
