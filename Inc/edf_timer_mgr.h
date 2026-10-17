/**
 ******************************************************************************
 * @file    edf_timer_mgr.h
 * @brief   定时器管理器接口
 ******************************************************************************
 */

#ifndef EDF_TIMER_MGR_H
#define EDF_TIMER_MGR_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/

#include "edf_base.h"
#include "edf_task_mgr.h"

/* Exported constants --------------------------------------------------------*/

/* Exported types ------------------------------------------------------------*/

/* 定时器句柄类型 */
typedef struct timer_blk timer_t;

/* Exported defines ----------------------------------------------------------*/

/**
 * @brief 毫秒转系统节拍数
 * @param ms 毫秒数
 */
#define TIMER_MS(ms)        ((timer_tick_t)((ms) * EDF_TICK_FREQ_HZ / 1000))

/**
 * @brief 秒转系统节拍数
 * @param sec 秒数
 */
#define TIMER_SEC(sec)      ((timer_tick_t)((sec) * EDF_TICK_FREQ_HZ))

/**
 * @brief 计算从 start 时刻起经过的节拍数
 * @param start 起始时刻（由 timer_get_tick() 获取）
 * @note 无符号减法抗 tick 回绕，前提是间隔小于 2^31 个节拍
 */
#define TIMER_ELAPSED(start)    ((timer_tick_t)(timer_get_tick() - (start)))

/**
 * @brief 判断从 start 时刻起是否已经超过 timeout 个节拍
 * @param start   起始时刻（由 timer_get_tick() 获取）
 * @param timeout 超时阈值（节拍数）
 * @note 无符号减法抗 tick 回绕，前提是间隔小于 2^31 个节拍
 */
#define TIMER_TIMEOUT(start, timeout)   (TIMER_ELAPSED(start) >= (timeout))

/* Exported functions --------------------------------------------------------*/

/******************************************************************************/
/*                        软件定时器管理函数                                  */
/******************************************************************************/

/**
 * @brief 创建定时器
 * @param timer_mode   定时器模式（单次/周期）
 * @param timer_reload 定时周期（系统节拍数，不能为0）
 * @param notify_task  超时时通知的任务句柄，可为NULL
 * @param notify_event 超时时触发的事件标志，仅在 notify_task 不为NULL时有效
 * @return 成功返回定时器句柄，失败返回NULL
 * @note 创建后处于 IDLE 状态，需调用 timer_start() 启动
 * @note 禁止在中断上下文调用
 */
timer_t *timer_create(timer_mode_t timer_mode,
                      timer_tick_t timer_reload,
                      task_t *notify_task,
                      task_event_t notify_event);

/**
 * @brief 删除定时器并释放内存
 * @param timer 定时器句柄
 * @note 不应删除还在被使用的定句柄
 * @note 不可重复删除，删除后需将句柄置 NULL
 * @note 禁止在中断上下文调用
 */
void timer_delete(timer_t *timer);

/**
 * @brief 启动定时器
 * @param timer 定时器句柄
 * @return 成功返回EDF_SUCCESS，失败返回EDF_FAILURE
 * @note 已运行的定时器不会重复启动
 * @note 允许在中断上下文调用
 */
uint8_t timer_start(timer_t *timer);

/**
 * @brief 停止定时器
 * @param timer 定时器句柄
 * @return 成功返回EDF_SUCCESS，失败返回EDF_FAILURE
 * @note 已停止的定时器不会重复停止
 * @note 允许在中断上下文调用
 */
uint8_t timer_stop(timer_t *timer);

/**
 * @brief 设置定时器重载周期
 * @param timer        定时器句柄
 * @param timer_reload 新的定时周期（系统节拍数，不能为0）
 * @return 成功返回 EDF_SUCCESS，失败返回 EDF_FAILURE
 * @note 运行中修改：本次计时不变，下一次计时才使用新周期
 * @note IDLE 状态下修改：下次 timer_start() 时使用新周期
 * @note 允许在中断上下文调用
 */
uint8_t timer_set_reload(timer_t *timer, timer_tick_t timer_reload);

/**
 * @brief 重新启动定时器（重新开始计时）
 * @param timer 定时器句柄
 * @return 成功返回 EDF_SUCCESS，失败返回 EDF_FAILURE
 * @note 无论定时器当前处于何种状态，都停止后重新启动
 * @note 允许在中断上下文调用
 */
uint8_t timer_restart(timer_t *timer);

/**
 * @brief 获取系统当前节拍计数
 * @return 当前系统节拍计数（自系统启动以来的累计值）
 * @note 允许在中断上下文调用
 * @note 配合 TIMER_ELAPSED()/TIMER_TIMEOUT() 使用，天然抗回绕
 */
timer_tick_t timer_get_tick(void);

/**
 * @brief 获取定时器当前状态
 * @param timer 定时器句柄
 * @return 当前状态（TIMER_STATUS_IDLE/RUNNING/STOPPED/EXPIRED），参数无效返回 TIMER_STATUS_IDLE
 * @note 允许在中断上下文调用
 */
timer_status_t timer_get_status(timer_t *timer);

/**
 * @brief 获取定时器当前重载周期
 * @param timer 定时器句柄
 * @return 当前重载周期（节拍数），参数无效返回 0
 * @note 允许在中断上下文调用
 */
timer_tick_t timer_get_reload(timer_t *timer);

/**
 * @brief 获取定时器距离到期的剩余节拍数
 * @param timer 定时器句柄
 * @return 剩余节拍数；非 RUNNING 状态或参数无效返回 0
 * @note 实现为 O(N) 遍历 delta 链表，定时器数量较少时开销可接受
 * @note 允许在中断上下文调用
 */
timer_tick_t timer_get_remaining(timer_t *timer);

/**
 * @brief 更新所有软件定时器
 * @note 此函数需要由 hal_timer_isr() 调用，用户不应直接调用
 * @note 必须在中断上下文调用
 */
void timer_update(void);

#ifdef __cplusplus
}
#endif

#endif /* EDF_TIMER_MGR_H */
