/**
 ******************************************************************************
 * @file    edf_msg_mgr.c
 * @brief   消息管理器实现
 ******************************************************************************
 */

/* Includes ------------------------------------------------------------------*/

#include "edf_msg_mgr.h"
#include "edf_base.h"
#include "edf_mem_mgr.h"
#include "edf_task_mgr.h"

/* Private constants ---------------------------------------------------------*/

/* Private types -------------------------------------------------------------*/

/* 消息队列定义 */
struct msg_queue
{
    uint8_t *buffer;        	// 环形缓冲（创建时分配 msg_size × capacity）
    msg_len_t msg_size;      	// 单条消息的固定大小（字节）
    uint8_t overflow;        	// 溢出标志：队列满导致 send 失败时置位
    uint8_t reserved0[1];
    msg_count_t capacity;      	// 队列最大可容纳的消息条数
    msg_count_t head;          	// 读索引
    msg_count_t tail;          	// 写索引
    msg_count_t count;         	// 当前消息数
    task_t *notify_task;   		// 消息到达时通知的任务
    task_event_t notify_event; 	// 消息到达时触发的事件标志
};

/* Private variables ---------------------------------------------------------*/

/* Private defines -----------------------------------------------------------*/

/* Private function prototypes ----------------------------------------------*/

/* Exported constants --------------------------------------------------------*/

/* Exported variables --------------------------------------------------------*/

/* Exported functions --------------------------------------------------------*/

/**
 * @brief 创建消息队列
 */
msg_queue_t *msg_queue_create(msg_len_t msg_size,
                              msg_count_t capacity,
                              task_t *notify_task,
                              task_event_t notify_event)
{
    msg_queue_t *new_queue;

    /* 参数检查 */
    if (msg_size == 0 || capacity == 0)
    {
        return (msg_queue_t*)NULL;
    }

    /* 分配队列控制块 */
    new_queue = (msg_queue_t*)mem_malloc(sizeof(msg_queue_t));
    if (new_queue == (msg_queue_t*)NULL)
    {
        return (msg_queue_t*)NULL;
    }

    /* 一次性分配环形缓冲 */
    new_queue->buffer = (uint8_t*)mem_malloc((hal_size_t)msg_size * capacity);
    if (new_queue->buffer == (uint8_t*)NULL)
    {
        mem_free(new_queue);

        return (msg_queue_t*)NULL;
    }

    /* 初始化队列 */
    new_queue->msg_size     = msg_size;
    new_queue->capacity     = capacity;
    new_queue->head         = 0;
    new_queue->tail         = 0;
    new_queue->count        = 0;
    new_queue->overflow     = 0;
    new_queue->notify_task  = notify_task;
    new_queue->notify_event = notify_event;

    return new_queue;
}

/**
 * @brief 删除消息队列并释放内存
 */
void msg_queue_delete(msg_queue_t *queue)
{
    /* 参数检查 */
    if (queue == (msg_queue_t*)NULL)
    {
        return;
    }

    /* 释放环形缓冲和队列控制块 */
    mem_free(queue->buffer);
    mem_free(queue);
}

/**
 * @brief 发送消息到队列（拷贝固定长度数据）
 */
uint8_t msg_queue_send(msg_queue_t *queue, const void *data)
{
    msg_len_t i;

    /* 参数检查 */
    if (queue == (msg_queue_t*)NULL || data == (const void*)NULL)
    {
        return EDF_FAILURE;
    }

    HAL_ENTER_CRITICAL_SECTION();

    /* 队列满 */
    if (queue->count >= queue->capacity)
    {
        queue->overflow = 1;

        HAL_EXIT_CRITICAL_SECTION();

        return EDF_FAILURE;
    }

    /* 拷贝固定长度数据到写指针位置 */
    for (i = 0; i < queue->msg_size; i++)
    {
        queue->buffer[queue->tail * queue->msg_size + i] = ((const uint8_t*)data)[i];
    }

    /* 移动写指针 */
    queue->tail = (queue->tail + 1) % queue->capacity;
    queue->count++;

    HAL_EXIT_CRITICAL_SECTION();

    /* 触发绑定任务的消息事件 */
    if (queue->notify_task != (task_t*)NULL)
    {
        task_set_event(queue->notify_task, queue->notify_event);
    }

    return EDF_SUCCESS;
}

/**
 * @brief 从队列接收消息（拷贝固定长度数据）
 */
uint8_t msg_queue_recv(msg_queue_t *queue, void *out)
{
    msg_len_t i;

    /* 参数检查 */
    if (queue == (msg_queue_t*)NULL || out == (void*)NULL)
    {
        return EDF_FAILURE;
    }

    HAL_ENTER_CRITICAL_SECTION();

    /* 队列空 */
    if (queue->count == 0)
    {
        HAL_EXIT_CRITICAL_SECTION();

        return EDF_FAILURE;
    }

    /* 从读指针位置拷贝固定长度数据 */
    for (i = 0; i < queue->msg_size; i++)
    {
        ((uint8_t*)out)[i] = queue->buffer[queue->head * queue->msg_size + i];
    }

    /* 移动读指针 */
    queue->head = (queue->head + 1) % queue->capacity;
    queue->count--;

    HAL_EXIT_CRITICAL_SECTION();

    return EDF_SUCCESS;
}

/**
 * @brief 窥视队首消息
 */
uint8_t msg_queue_peek(msg_queue_t *queue, void *out)
{
    msg_len_t i;

    if (queue == (msg_queue_t*)NULL || out == (void*)NULL)
    {
        return EDF_FAILURE;
    }

    HAL_ENTER_CRITICAL_SECTION();

    /* 队列空 */
    if (queue->count == 0)
    {
        HAL_EXIT_CRITICAL_SECTION();

        return EDF_FAILURE;
    }

    for (i = 0; i < queue->msg_size; i++)
    {
        ((uint8_t*)out)[i] = queue->buffer[queue->head * queue->msg_size + i];
    }

    HAL_EXIT_CRITICAL_SECTION();

    return EDF_SUCCESS;
}

/**
 * @brief 获取队列当前消息数
 */
msg_count_t msg_queue_get_count(msg_queue_t *queue)
{
    if (queue == (msg_queue_t*)NULL)
    {
        return 0;
    }

    return queue->count;
}

/**
 * @brief 获取队列剩余容量
 */
msg_count_t msg_queue_get_free(msg_queue_t *queue)
{
    if (queue == (msg_queue_t*)NULL)
    {
        return 0;
    }

    return (msg_count_t)(queue->capacity - queue->count);
}

/**
 * @brief 判断队列是否为空
 */
uint8_t msg_queue_is_empty(msg_queue_t *queue)
{
    if (queue == (msg_queue_t*)NULL)
    {
        return EDF_FAILURE;
    }

    return (queue->count == 0) ? EDF_SUCCESS : EDF_FAILURE;
}

/**
 * @brief 判断队列是否已满
 */
uint8_t msg_queue_is_full(msg_queue_t *queue)
{
    if (queue == (msg_queue_t*)NULL)
    {
        return EDF_FAILURE;
    }

    return (queue->count >= queue->capacity) ? EDF_SUCCESS : EDF_FAILURE;
}

/**
 * @brief 检查并清除溢出标志
 */
uint8_t msg_queue_check_overflow(msg_queue_t *queue)
{
    uint8_t result;

    if (queue == (msg_queue_t*)NULL)
    {
        return EDF_FAILURE;
    }

    HAL_ENTER_CRITICAL_SECTION();

    result = (queue->overflow != 0) ? EDF_SUCCESS : EDF_FAILURE;
    queue->overflow = 0;

    HAL_EXIT_CRITICAL_SECTION();

    return result;
}

/**
 * @brief 丢弃若干条队首消息
 */
uint8_t msg_queue_skip(msg_queue_t *queue, msg_count_t n)
{
    msg_count_t avail;

    if (queue == (msg_queue_t*)NULL)
    {
        return EDF_FAILURE;
    }

    HAL_ENTER_CRITICAL_SECTION();

    avail = queue->count;
    if (n > avail)
    {
        n = avail;
    }

    queue->head  = (queue->head + n) % queue->capacity;
    queue->count -= n;

    HAL_EXIT_CRITICAL_SECTION();

    return EDF_SUCCESS;
}

/**
 * @brief 清空队列中的所有消息
 */
uint8_t msg_queue_flush(msg_queue_t *queue)
{
    if (queue == (msg_queue_t*)NULL)
    {
        return EDF_FAILURE;
    }

    HAL_ENTER_CRITICAL_SECTION();

    queue->head  = queue->tail;
    queue->count = 0;

    HAL_EXIT_CRITICAL_SECTION();

    return EDF_SUCCESS;
}

/* Private functions --------------------------------------------------------*/
