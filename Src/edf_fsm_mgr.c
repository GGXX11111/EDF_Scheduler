/**
 ******************************************************************************
 * @file    edf_fsm_mgr.c
 * @brief   有限状态机管理器实现
 ******************************************************************************
 */

/* Includes ------------------------------------------------------------------*/

#include "edf_fsm_mgr.h"
#include "edf_base.h"

/* Private constants ---------------------------------------------------------*/

/* Private types -------------------------------------------------------------*/

/* Private variables ---------------------------------------------------------*/

/* Private defines -----------------------------------------------------------*/

/* Private function prototypes ----------------------------------------------*/

static const fsm_transition_t *fsm_find_transition(const fsm_t *fsm, fsm_event_t event);

/* Exported constants --------------------------------------------------------*/

/* Exported variables --------------------------------------------------------*/

/* Exported functions --------------------------------------------------------*/

/**
 * @brief 初始化状态机（静态创建）
 */
fsm_t *fsm_init(fsm_t *fsm,
                fsm_state_t initial_state,
                const fsm_transition_t *table,
                uint16_t table_size,
                void *user_data)
{
    uint16_t i;

    /* 参数检查 */
    if (fsm == (fsm_t*)NULL ||
        table == (const fsm_transition_t*)NULL ||
        table_size == 0U ||
        initial_state >= FSM_STATE_SELF ||
        fsm->inited != 0)
    {
        return (fsm_t*)NULL;
    }

    /* 校验转移表 */
    for (i = 0U; i < table_size; i++)
    {
        if ((table[i].from >= FSM_STATE_SELF) ||
            (table[i].to == FSM_STATE_NONE) ||
            (table[i].event == FSM_EVENT_NULL))
        {
            return (fsm_t*)NULL;
        }
    }

    /* 初始化状态机块 */
    fsm->table          = table;
    fsm->table_size     = table_size;
    fsm->initial_state  = initial_state;
    fsm->current_state  = initial_state;
    fsm->user_data      = user_data;
    fsm->busy           = 0;
    fsm->inited         = 1;

    return fsm;
}

/**
 * @brief 派发一个事件
 */
edf_status_t fsm_dispatch(fsm_t *fsm, fsm_event_t event)
{
    const fsm_transition_t *transition;

    /* 参数检查 */
    if (fsm == (fsm_t*)NULL || 
		fsm->inited == 0 || 
		event == FSM_EVENT_NULL)
    {
        return EDF_ERR;
    }

    /* 转移动作内部再次派发，直接拒绝，避免状态被破坏 */
    if (fsm->busy != 0)
    {
        return EDF_ERR;
    }

    /* 按表顺序查找第一条匹配的转移 */
    transition = fsm_find_transition(fsm, event);

    if (transition == (const fsm_transition_t*)NULL)
    {
        /* 当前状态不响应此事件，状态保持不变 */
        return EDF_ERR;
    }

    fsm->busy = 1;

    /* 先更新状态，再执行动作，保证动作内查询到的是目标状态 */
    if (transition->to != FSM_STATE_SELF)
    {
        fsm->current_state = transition->to;
    }

    if (transition->action != (fp_fsm_action)NULL)
    {
        transition->action(fsm, event);
    }

    fsm->busy = 0;

    return EDF_OK;
}

/**
 * @brief 复位状态机到初始状态
 */
edf_status_t fsm_reset(fsm_t *fsm)
{
    /* 参数检查 */
    if (fsm == (fsm_t*)NULL || 
		fsm->inited == 0)
    {
        return EDF_ERR;
    }

    /* 转移动作执行中不允许复位，避免状态被动作覆盖 */
    if (fsm->busy != 0)
    {
        return EDF_ERR;
    }

    /* 只恢复状态，不执行任何转移动作 */
    fsm->current_state = fsm->initial_state;

    return EDF_OK;
}

/**
 * @brief 获取当前状态
 */
fsm_state_t fsm_get_state(const fsm_t *fsm)
{
    if (fsm == (const fsm_t*)NULL || fsm->inited == 0)
    {
        return FSM_STATE_NONE;
    }

    return fsm->current_state;
}

/**
 * @brief 判断当前状态
 */
edf_bool_t fsm_is_state(const fsm_t *fsm, fsm_state_t state)
{
    if (fsm == (const fsm_t*)NULL || fsm->inited == 0)
    {
        return EDF_FALSE;
    }

    return (fsm->current_state == state) ? EDF_TRUE : EDF_FALSE;
}

/* Private functions --------------------------------------------------------*/

/**
 * @brief 查找匹配的转移表项
 * @param fsm   状态机句柄
 * @param event 事件标志
 * @return 找到返回表项指针，未找到返回NULL
 * @note 按表顺序首次匹配，调用者可把高频转移放在表的前部
 */
static const fsm_transition_t *fsm_find_transition(const fsm_t *fsm, fsm_event_t event)
{
    uint16_t i;

    for (i = 0U; i < fsm->table_size; i++)
    {
        if ((fsm->table[i].from == fsm->current_state) &&
            ((fsm->table[i].event & event) != 0U))
        {
            return &fsm->table[i];
        }
    }

    return (const fsm_transition_t*)NULL;
}
