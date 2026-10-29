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
typedef uint32_t	timer_tick_t;       // 时钟节拍类型

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

/* 定时器句柄类型 */
typedef struct timer_blk timer_t;

/**
 * @brief 定时器到期实时动作
 * @note 在定时器中断上下文中执行，动作务必简短：
 *       禁止阻塞、禁止耗时循环、禁止调用非中断安全的接口
 */
typedef void (*fp_timer_action_func)(timer_t *timer);

/**
 * @brief 定时器控制块
 */
struct timer_blk
{
    struct timer_blk * volatile next;       // 链表下一个节点
    struct timer_blk * volatile prev;       // 链表上一个节点
    volatile timer_mode_t timer_mode;     	// 定时器模式
    volatile timer_status_t timer_status;   // 定时器状态
    uint8_t inited;                         // 初始化标志
	uint8_t reserved0[3];
    volatile timer_tick_t timer_reload;     // 重载值（周期）
    volatile timer_tick_t delta_ticks;      // 距离前一个定时器到期的剩余节拍数（Delta List）
    task_t *notify_task;                    // 超时时通知的任务
    task_event_t notify_event;              // 超时时触发的事件标志
    fp_timer_action_func real_time_action;  // 到期实时动作（中断上下文，务必简短）
};

/* Exported defines ----------------------------------------------------------*/

/**
 * @brief 定义定时器控制块，零初始化
 */
#define TIMER_DEFINE(timer)		timer_t timer = {0}	

/**
 * @brief 毫秒转系统节拍数
 * @param ms 毫秒数
 */
#define TIMER_MS(ms)        	((timer_tick_t)((ms) * EDF_TICK_FREQ_HZ / 1000))

/**
 * @brief 秒转系统节拍数
 * @param sec 秒数
 */
#define TIMER_SEC(sec)      	((timer_tick_t)((sec) * EDF_TICK_FREQ_HZ))

/* Exported functions --------------------------------------------------------*/

/**
 * @brief 初始化定时器（静态创建）
 * @param timer        		调用者提供的定时器控制块，必须零初始化
 * @param timer_mode   		定时器模式（单次/周期）
 * @param timer_reload 		定时周期（系统节拍数，不能为0）
 * @param notify_task  		超时时通知的任务句柄，可为NULL
 * @param notify_event 		超时时触发的事件标志，仅在 notify_task 不为NULL时有效
 * @param real_time_action 	到期实时动作，可为NULL
 * @return 成功返回 timer，参数无效或重复初始化返回NULL
 * @note 初始化后处于 IDLE 状态，需调用 timer_start() 启动
 * @note 初始化后长期存在，没有对应的销毁接口
 * @note real_time_action 在中断上下文中执行，动作务必简短：
 *       禁止阻塞、禁止耗时循环、禁止调用非中断安全的接口
 * @note 禁止在中断上下文调用
 */
timer_t *timer_init(timer_t *timer,
                    timer_mode_t timer_mode,
                    timer_tick_t timer_reload,
                    task_t *notify_task,
                    task_event_t notify_event,
                    fp_timer_action_func real_time_action);

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
 * @brief 修改定时器模式
 * @param timer      定时器句柄
 * @param timer_mode 新的定时器模式
 * @return 成功返回EDF_OK，失败返回EDF_ERR
 * @note 运行中的定时器在当前周期到期后按新模式生效，不打断正在进行的计时
 * @note IDLE/STOPPED/EXPIRED 状态只更新模式，下次 timer_start() 生效
 * @note 允许在中断上下文调用
 */
edf_status_t timer_set_mode(timer_t *timer, timer_mode_t timer_mode);

/**
 * @brief 修改定时器重载值
 * @param timer        定时器句柄
 * @param timer_reload 新的定时周期
 * @return 成功返回EDF_OK，失败返回EDF_ERR
 * @note 运行中的定时器立即按新周期重新计时，旧周期的剩余节拍被丢弃
 * @note IDLE/STOPPED/EXPIRED 状态只更新重载值，下次 timer_start() 生效
 * @note 允许在中断上下文调用
 */
edf_status_t timer_set_reload(timer_t *timer, timer_tick_t timer_reload);

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
