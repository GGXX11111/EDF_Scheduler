/**
 ******************************************************************************
 * @file    edf_msg_mgr.h
 * @brief   消息管理器接口
 ******************************************************************************
 */

#ifndef EDF_MSG_MGR_H
#define EDF_MSG_MGR_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/

#include "edf_base.h"
#include "edf_task_mgr.h"

/* Exported types ------------------------------------------------------------*/

/* 消息相关类型 */
typedef uint16_t	msg_count_t;	// 消息数量类型
typedef uint16_t	msg_len_t;  	// 消息长度类型

/* 消息队列句柄类型 */
typedef struct msg_queue msg_queue_t;

/**
 * @brief 消息队列控制块
 */
struct msg_queue
{
    uint8_t *buffer;            	// 环形缓冲（由调用者提供，大小 >= msg_size × capacity）
    msg_len_t msg_size;         	// 单条消息的固定大小（字节）
    uint8_t inited;    				// 初始化标志
	uint8_t reserved0[1];
    msg_count_t capacity;       	// 最大可容纳的消息条数（必须为 2 的幂）
    volatile msg_count_t head;  	// 读索引
    volatile msg_count_t tail;  	// 写索引
    volatile msg_count_t count; 	// 当前消息数
    task_t *notify_task;        	// 消息到达时通知的任务
    task_event_t notify_event;  	// 消息到达时触发的事件标志
} ;

/* Exported defines ----------------------------------------------------------*/

/**
 * @brief 定义消息队列控制块，零初始化
 */
#define MSGQ_DEFINE(msg_queue)	msg_queue_t msg_queue = {0}

/* Exported functions --------------------------------------------------------*/

/**
 * @brief 初始化消息队列（静态创建）
 * @param queue        调用者提供的队列控制块，必须零初始化
 * @param buffer       调用者提供的环形缓冲，长度必须 >= msg_size × capacity
 * @param msg_size     单条消息的固定大小（字节，不能为 0）
 * @param capacity     队列最大可容纳的消息条数，必须为 2 的幂
 * @param notify_task  消息到达时通知的任务句柄，可为NULL
 * @param notify_event 消息到达时触发的事件标志，仅在 notify_task 不为NULL时有效
 * @return 成功返回 queue，参数无效或重复初始化返回NULL
 * @note 初始化后长期存在，没有对应的销毁接口
 * @note 禁止在中断上下文调用
 */
msg_queue_t *msg_queue_init(msg_queue_t *queue,
                            uint8_t *buffer,
                            msg_len_t msg_size,
                            msg_count_t capacity,
                            task_t *notify_task,
                            task_event_t notify_event);

/**
 * @brief 发送消息到队列（拷贝固定长度数据）
 * @param queue 目标队列句柄
 * @param data  待发送的数据指针（长度固定为创建时的 msg_size）
 * @return 成功返回EDF_OK，失败返回EDF_ERR
 * @note 发送后若队列绑定了通知任务，会触发该任务的 notify_event 事件
 * @note 允许在中断上下文调用
 */
edf_status_t msg_queue_send(msg_queue_t *queue, const void *data);

/**
 * @brief 从队列接收消息（拷贝固定长度数据）
 * @param queue 目标队列句柄
 * @param out   数据输出缓冲区（长度固定为创建时的 msg_size）
 * @return 成功返回EDF_OK，失败返回EDF_ERR
 * @note 允许在中断上下文调用
 */
edf_status_t msg_queue_recv(msg_queue_t *queue, void *out);

/**
 * @brief 窥视队首消息
 * @param queue 队列句柄
 * @param out   数据输出缓冲区（长度固定为创建时的 msg_size）
 * @return 成功返回EDF_OK，失败返回EDF_ERR
 * @note 与 msg_queue_recv 行为一致，但不移动读索引、不更新计数
 * @note 允许在中断上下文调用
 */
edf_status_t msg_queue_peek(msg_queue_t *queue, void *out);

/**
 * @brief 获取队列当前消息数
 * @param queue 队列句柄
 * @return 当前消息数，参数无效返回 0
 * @note 允许在中断上下文调用
 */
msg_count_t msg_queue_get_count(msg_queue_t *queue);

/**
 * @brief 清空队列中的所有消息
 * @param queue 队列句柄
 * @return 成功返回EDF_OK，失败返回EDF_ERR
 * @note 允许在中断上下文调用
 */
edf_status_t msg_queue_flush(msg_queue_t *queue);

#ifdef __cplusplus
}
#endif

#endif /* EDF_MSG_MGR_H */
