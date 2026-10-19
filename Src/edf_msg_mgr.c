/**
 ******************************************************************************
 * @file    edf_msg_mgr.c
 * @brief   消息管理器实现
 ******************************************************************************
 */

/* Includes ------------------------------------------------------------------*/

#include "edf_msg_mgr.h"
#include "edf_base.h"
#include "edf_task_mgr.h"

/* Private constants ---------------------------------------------------------*/

/* Private types -------------------------------------------------------------*/

/* Private variables ---------------------------------------------------------*/

/* Private defines -----------------------------------------------------------*/

/**
 * @brief 检查队列容量是否为 2 的幂（内部索引取模依赖此约束）
 */
#define QUEUE_CAP_IS_POW2(n)    (((n) != 0) && (((n) & ((n) - 1U)) == 0U))

/* Private function prototypes ----------------------------------------------*/

/* Exported constants --------------------------------------------------------*/

/* Exported variables --------------------------------------------------------*/

/* Exported functions --------------------------------------------------------*/

/**
 * @brief 初始化消息队列（静态创建）
 */
msg_queue_t *msg_queue_init(msg_queue_t *queue,
                            uint8_t *buffer,
                            msg_len_t msg_size,
                            msg_count_t capacity,
                            task_t *notify_task,
                            task_event_t notify_event)
{
    /* 参数检查：容量必须为 2 的幂（内部索引取模依赖此约束） */
    if (queue == (msg_queue_t*)NULL || buffer == (uint8_t*)NULL ||
        msg_size == 0 || capacity == 0 || !QUEUE_CAP_IS_POW2(capacity))
    {
        return (msg_queue_t*)NULL;
    }

    /* 已初始化的控制块不允许重复初始化，避免覆盖队列内部状态 */
    if (queue->inited != 0)
    {
        return (msg_queue_t*)NULL;
    }

    /* 初始化队列 */
    queue->buffer 		= buffer;
    queue->msg_size     = msg_size;
    queue->capacity     = capacity;
    queue->head         = 0;
    queue->tail         = 0;
    queue->count        = 0;
    queue->notify_task  = notify_task;
    queue->notify_event = notify_event;
    queue->inited       = 1;

    return queue;
}

/**
 * @brief 发送消息到队列（拷贝固定长度数据）
 */
edf_status_t msg_queue_send(msg_queue_t *queue, const void *data)
{
    msg_len_t i;

    /* 参数检查 */
    if (queue == (msg_queue_t*)NULL || queue->inited == 0 || data == (const void*)NULL)
    {
        return EDF_ERR;
    }

    HAL_ENTER_CRITICAL_SECTION();

    /* 队列满 */
    if (queue->count >= queue->capacity)
    {
        HAL_EXIT_CRITICAL_SECTION();

        return EDF_ERR;
    }

    /* 拷贝固定长度数据到写指针位置 */
    for (i = 0; i < queue->msg_size; i++)
    {
        queue->buffer[queue->tail * queue->msg_size + i] = ((const uint8_t*)data)[i];
    }

    /* 移动写指针 */
    queue->tail = ((queue->tail + 1U) & (queue->capacity - 1U));
    queue->count++;

    HAL_EXIT_CRITICAL_SECTION();

    /* 触发绑定任务的消息事件 */
    if (queue->notify_task != (task_t*)NULL)
    {
        task_set_event(queue->notify_task, queue->notify_event);
    }

    return EDF_OK;
}

/**
 * @brief 从队列接收消息（拷贝固定长度数据）
 */
edf_status_t msg_queue_recv(msg_queue_t *queue, void *out)
{
    msg_len_t i;

    /* 参数检查 */
    if (queue == (msg_queue_t*)NULL || queue->inited == 0 || out == (void*)NULL)
    {
        return EDF_ERR;
    }

    HAL_ENTER_CRITICAL_SECTION();

    /* 队列空 */
    if (queue->count == 0)
    {
        HAL_EXIT_CRITICAL_SECTION();

        return EDF_ERR;
    }

    /* 从读指针位置拷贝固定长度数据 */
    for (i = 0; i < queue->msg_size; i++)
    {
        ((uint8_t*)out)[i] = queue->buffer[queue->head * queue->msg_size + i];
    }

    /* 移动读指针 */
    queue->head = ((queue->head + 1U) & (queue->capacity - 1U));
    queue->count--;

    HAL_EXIT_CRITICAL_SECTION();

    return EDF_OK;
}

/**
 * @brief 窥视队首消息
 */
edf_status_t msg_queue_peek(msg_queue_t *queue, void *out)
{
    msg_len_t i;

    if (queue == (msg_queue_t*)NULL || queue->inited == 0 || out == (void*)NULL)
    {
        return EDF_ERR;
    }

    HAL_ENTER_CRITICAL_SECTION();

    /* 队列空 */
    if (queue->count == 0)
    {
        HAL_EXIT_CRITICAL_SECTION();

        return EDF_ERR;
    }

    for (i = 0; i < queue->msg_size; i++)
    {
        ((uint8_t*)out)[i] = queue->buffer[queue->head * queue->msg_size + i];
    }

    HAL_EXIT_CRITICAL_SECTION();

    return EDF_OK;
}

/**
 * @brief 获取队列当前消息数
 */
msg_count_t msg_queue_get_count(msg_queue_t *queue)
{
    if (queue == (msg_queue_t*)NULL || queue->inited == 0)
    {
        return 0;
    }

    return queue->count;
}

/**
 * @brief 清空队列中的所有消息
 */
edf_status_t msg_queue_flush(msg_queue_t *queue)
{
    if (queue == (msg_queue_t*)NULL || queue->inited == 0)
    {
        return EDF_ERR;
    }

    HAL_ENTER_CRITICAL_SECTION();

    queue->head  = queue->tail;
    queue->count = 0;

    HAL_EXIT_CRITICAL_SECTION();

    return EDF_OK;
}

/* Private functions --------------------------------------------------------*/
