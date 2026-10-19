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

/* 定时器相关类型 */
typedef uint32_t                timer_tick_t;       // 时钟节拍类型

/* 硬件定时器状态 */
typedef enum timer_hw_state
{
    TIMER_HW_INACTIVE       = 0x00, 	// 未激活
    TIMER_HW_ACTIVE         = 0x01,  	// 已激活
} timer_hw_state_t;

/* 软件定时器模式 */
typedef enum timer_mode
{
    TIMER_MODE_SINGLE       = 0x00,		// 单次定时器
    TIMER_MODE_PERIODIC     = 0x01, 	// 周期定时器
} timer_mode_t;

/* 软件定时器状态 */
typedef enum timer_status
{
    TIMER_STATUS_IDLE       = 0x00,		// 空闲
    TIMER_STATUS_RUNNING    = 0x01,		// 运行中
    TIMER_STATUS_STOPPED    = 0x02,		// 已停止
    TIMER_STATUS_EXPIRED    = 0x03,		// 已超时
} timer_status_t;

/**
 * @brief 定时器控制块
 */
typedef struct timer_blk
{
    struct timer_blk *next;         // 链表下一个节点
    struct timer_blk *prev;         // 链表上一个节点
    timer_mode_t timer_mode;        // 定时器模式
    timer_status_t timer_status;    // 定时器状态
    uint8_t inited;                 // 初始化标志：0-未初始化，1-已初始化
	uint8_t reserved0[3];
    timer_tick_t timer_reload;      // 重载值（周期）
    timer_tick_t delta_ticks;       // 距离前一个定时器到期的剩余节拍数（Delta List）
    task_t *notify_task;            // 超时时通知的任务
    task_event_t notify_event;      // 超时时触发的事件标志
} timer_t;

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

/* Exported functions --------------------------------------------------------*/

/******************************************************************************/
/*                        软件定时器管理函数                                  */
/******************************************************************************/

/**
 * @brief 初始化定时器（静态创建）
 * @param timer        调用者提供的定时器控制块（全局或静态变量）
 * @param timer_mode   定时器模式（单次/周期）
 * @param timer_reload 定时周期（系统节拍数，不能为0）
 * @param notify_task  超时时通知的任务句柄，可为NULL
 * @param notify_event 超时时触发的事件标志，仅在 notify_task 不为NULL时有效
 * @return 成功返回 timer，参数无效返回NULL
 * @note 初始化后处于 IDLE 状态，需调用 timer_start() 启动
 * @note 同一个 timer 只能初始化一次，重复 init 会返回 NULL
 * @note 初始化后长期存在，没有对应的销毁接口；
 * @note 禁止在中断上下文调用
 */
timer_t *timer_init(timer_t *timer,
                    timer_mode_t timer_mode,
                    timer_tick_t timer_reload,
                    task_t *notify_task,
                    task_event_t notify_event);

/**
 * @brief 启动定时器
 * @param timer 定时器句柄
 * @return 成功返回EDF_OK，失败返回EDF_ERR
 * @note 已运行的定时器不会重复启动
 * @note 允许在中断上下文调用
 */
edf_status_t timer_start(timer_t *timer);

/**
 * @brief 停止定时器
 * @param timer 定时器句柄
 * @return 成功返回EDF_OK，失败返回EDF_ERR
 * @note 已停止的定时器不会重复停止
 * @note 允许在中断上下文调用
 */
edf_status_t timer_stop(timer_t *timer);

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
