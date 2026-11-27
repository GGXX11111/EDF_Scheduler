/**
 ******************************************************************************
 * @file    edf_fsm_mgr.h
 * @brief   有限状态机管理器接口
 ******************************************************************************
 */

#ifndef EDF_FSM_MGR_H
#define EDF_FSM_MGR_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/

#include "edf_base.h"

/* Exported constants --------------------------------------------------------*/

/* 保留状态值，用户状态编号不得占用 */
#define FSM_STATE_NONE		0xFFU	// 无效状态
#define FSM_STATE_SELF		0xFEU	// 内部转移：保持当前状态

/* 事件空值 */
#define FSM_EVENT_NULL		0x00000000

/* Exported types ------------------------------------------------------------*/

/* 状态编号类型，有效范围 0x00 ~ 0xFD */
typedef uint8_t		fsm_state_t;

/* 状态机事件类型，单 bit 表示一个事件 */
typedef uint32_t	fsm_event_t;

/* 状态机句柄类型 */
typedef struct fsm fsm_t;

/**
 * @brief 转移动作
 * @param fsm   状态机句柄
 * @param event 触发本次转移的事件
 * @note 在 fsm_dispatch() 内同步执行，执行时 fsm->current_state 已经是目标状态
 * @note 禁止阻塞、禁止耗时循环、禁止在内部再次调用 fsm_dispatch()
 * @note 若本次派发发生在中断上下文，本动作必须是中断安全的
 */
typedef void (*fp_fsm_action)(fsm_t *fsm, fsm_event_t event);

/**
 * @brief 转移表项
 */
typedef struct
{
    fsm_state_t   from;      // 源状态
    fsm_event_t   event;     // 触发事件
    fsm_state_t   to;        // 目标状态，FSM_STATE_SELF 表示内部转移
    fp_fsm_action action;    // 转移动作，可为 NULL
} fsm_transition_t;

/**
 * @brief 状态机控制块
 */
struct fsm
{
    const fsm_transition_t *table; 		// 转移表（const，可放入 flash）
    uint16_t table_size; 				// 转移表条目数
    volatile fsm_state_t current_state; // 当前状态
    fsm_state_t initial_state; 			// 初始状态
    void *user_data; 					// 用户上下文，透传给转移动作
    uint8_t inited; 					// 初始化标志
    volatile uint8_t busy; 				// 重入保护
    uint8_t reserved[2];
};

/* Exported defines ----------------------------------------------------------*/

/**
 * @brief 定义状态机控制块，零初始化
 */
#define FSM_DEFINE(fsm)			fsm_t fsm = {0}

/* Exported functions --------------------------------------------------------*/

/**
 * @brief 初始化状态机（静态创建）
 * @param fsm           调用者提供的控制块，必须零初始化
 * @param initial_state 初始状态
 * @param table         转移表，必须为 const 静态数组
 * @param table_size    转移表条目数
 * @param user_data     用户上下文指针，可为 NULL
 * @return 成功返回 fsm，参数无效、转移表非法或重复初始化返回 NULL
 * @note initial_state 必须是有效状态（0x00 ~ 0xFD），不能为 FSM_STATE_SELF / FSM_STATE_NONE
 * @note 初始化时逐条校验转移表：from 必须为有效状态，to 不得为 FSM_STATE_NONE，
 *       event 不得为 FSM_EVENT_NULL，否则返回 NULL
 * @note 初始化后fsm处于 initial_state，但不执行任何动作；
 *       需要进入初始状态的初始化动作时，由调用者在 fsm_init() 之后自行完成
 * @note 初始化后长期存在，没有对应的销毁接口
 * @note 禁止在中断上下文调用
 */
fsm_t *fsm_init(fsm_t *fsm,
                fsm_state_t initial_state,
                const fsm_transition_t *table,
                uint16_t table_size,
                void *user_data);

/**
 * @brief 派发一个事件
 * @param fsm   状态机句柄
 * @param event 事件标志，一次只应包含一个事件位
 * @return 发生转移返回 EDF_OK；无匹配转移、重入或参数无效返回 EDF_ERR
 * @note 按转移表顺序查找第一条 from 与当前状态匹配、且 event 位与运算非零的条目
 * @note 先更新 current_state，再执行转移动作
 * @note 转移动作为 NULL 时只改变状态
 * @note 允许在中断上下文调用，此时要求转移动作同样中断安全
 * @note 执行上下文独占：控制块中的 busy 只是重入保护，不提供跨上下文互斥。
 *       同一个 fsm_t 实例只允许由一个执行上下文调用本函数——任务 handler、
 *       某一个固定 ISR，或某一组不会相互抢占的同优先级 ISR，三者只可选其一。
 * @note 若任务与中断、或可嵌套的不同优先级中断需要共享同一实例，必须由调用者
 *       在更高层加临界区，且只包住本函数调用，不得包住转移动作；中断侧应使用
 *       BASEPRI 方式，避免 PRIMASK 屏蔽全部中断。
 * @note 单事件位与不重入的约定由调用者保证，框架内部不做断言或检查
 */
edf_status_t fsm_dispatch(fsm_t *fsm, fsm_event_t event);

/**
 * @brief 复位状态机到初始状态
 * @param fsm 状态机句柄
 * @return 成功返回 EDF_OK；参数无效、未初始化或重入返回 EDF_ERR
 * @note 只修改 current_state 为 initial_state，不执行任何转移动作；
 *       需要恢复初始状态的初始化动作时由调用者自行完成
 * @note 与 fsm_dispatch() 必须处于同一执行上下文，不可在转移动作内调用
 */
edf_status_t fsm_reset(fsm_t *fsm);

/**
 * @brief 获取当前状态
 * @param fsm 状态机句柄
 * @return 当前状态，参数无效返回 FSM_STATE_NONE
 * @note 允许在中断上下文调用
 */
fsm_state_t fsm_get_state(const fsm_t *fsm);

/**
 * @brief 判断当前状态
 * @param fsm   状态机句柄
 * @param state 待比较的状态
 * @return 相返回 EDF_TRUE，否则返回 EDF_FALSE
 * @note 允许在中断上下文调用
 */
edf_bool_t fsm_is_state(const fsm_t *fsm, fsm_state_t state);

#ifdef __cplusplus
}
#endif

#endif /* EDF_FSM_MGR_H */
