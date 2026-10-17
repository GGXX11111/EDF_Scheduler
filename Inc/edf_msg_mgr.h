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

/* 消息队列句柄类型 */
typedef struct msg_queue msg_queue_t;

/* Exported defines ----------------------------------------------------------*/

/* Exported functions --------------------------------------------------------*/

/******************************************************************************/
/*                        消息管理函数                                        */
/******************************************************************************/

/**
 * @brief 创建消息队列
 * @param msg_size     单条消息的固定大小（字节）
 * @param capacity     队列最大可容纳的消息条数
 * @param notify_task  消息到达时通知的任务句柄，可为NULL
 * @param notify_event 消息到达时触发的事件标志，仅在 notify_task 不为NULL时有效
 * @return 成功返回队列句柄，失败返回NULL
 * @note 禁止在中断上下文调用
 */
msg_queue_t *msg_queue_create(msg_len_t msg_size,
                              msg_count_t capacity,
                              task_t *notify_task,
                              task_event_t notify_event);

/**
 * @brief 删除消息队列并释放内存
 * @param queue 队列句柄
 * @note 不应删除还在被使用的句柄
 * @note 不可重复删除，删除后需将句柄置 NULL
 * @note 禁止在中断上下文调用
 */
void msg_queue_delete(msg_queue_t *queue);

/**
 * @brief 发送消息到队列（拷贝固定长度数据）
 * @param queue 目标队列句柄
 * @param data  待发送的数据指针（长度固定为创建时的 msg_size）
 * @return 成功返回EDF_SUCCESS，失败返回EDF_FAILURE
 * @note 发送后若队列绑定了通知任务，会触发该任务的 notify_event 事件
 * @note 允许在中断上下文调用
 */
uint8_t msg_queue_send(msg_queue_t *queue, const void *data);

/**
 * @brief 从队列接收消息（拷贝固定长度数据）
 * @param queue 目标队列句柄
 * @param out   数据输出缓冲区（长度固定为创建时的 msg_size）
 * @return 成功返回EDF_SUCCESS，失败返回EDF_FAILURE
 * @note 允许在中断上下文调用
 */
uint8_t msg_queue_recv(msg_queue_t *queue, void *out);

/**
 * @brief 窥视队首消息
 * @param queue 队列句柄
 * @param out   数据输出缓冲区（长度固定为创建时的 msg_size）
 * @return 成功返回EDF_SUCCESS，失败返回EDF_FAILURE
 * @note 与 msg_queue_recv 行为一致，但不移动读索引、不更新计数
 * @note 允许在中断上下文调用
 */
uint8_t msg_queue_peek(msg_queue_t *queue, void *out);

/**
 * @brief 获取队列当前消息数
 * @param queue 队列句柄
 * @return 当前消息数，参数无效返回 0
 * @note 允许在中断上下文调用
 */
msg_count_t msg_queue_get_count(msg_queue_t *queue);

/**
 * @brief 获取队列剩余容量
 * @param queue 队列句柄
 * @return 可容纳的消息条数，参数无效返回 0
 * @note 允许在中断上下文调用
 */
msg_count_t msg_queue_get_free(msg_queue_t *queue);

/**
 * @brief 判断队列是否为空
 * @param queue 队列句柄
 * @return EDF_SUCCESS 表示空，EDF_FAILURE 表示非空或参数无效
 * @note 允许在中断上下文调用
 */
uint8_t msg_queue_is_empty(msg_queue_t *queue);

/**
 * @brief 判断队列是否已满
 * @param queue 队列句柄
 * @return EDF_SUCCESS 表示已满，EDF_FAILURE 表示未满或参数无效
 * @note 允许在中断上下文调用
 */
uint8_t msg_queue_is_full(msg_queue_t *queue);

/**
 * @brief 检查并清除溢出标志
 * @param queue 队列句柄
 * @return EDF_SUCCESS 表示曾因队列满导致 send 失败，EDF_FAILURE 表示无溢出或参数无效
 * @note 调用本函数会清除溢出标志
 * @note 允许在中断上下文调用
 */
uint8_t msg_queue_check_overflow(msg_queue_t *queue);

/**
 * @brief 丢弃若干条队首消息
 * @param queue 队列句柄
 * @param n     请求丢弃的消息条数
 * @return 成功返回EDF_SUCCESS，失败返回EDF_FAILURE
 * @note 不足时仅丢弃已有部分，不算失败
 * @note 允许在中断上下文调用
 */
uint8_t msg_queue_skip(msg_queue_t *queue, msg_count_t n);

/**
 * @brief 清空队列中的所有消息
 * @param queue 队列句柄
 * @return 成功返回EDF_SUCCESS，失败返回EDF_FAILURE
 * @note 不清除 overflow 标志（溢出属于历史事件，与数据状态独立）
 * @note 允许在中断上下文调用
 */
uint8_t msg_queue_flush(msg_queue_t *queue);

#ifdef __cplusplus
}
#endif

#endif /* EDF_MSG_MGR_H */
