/**
 ******************************************************************************
 * @file    edf_stream_mgr.c
 * @brief   字节流管理器实现
 ******************************************************************************
 */

/* Includes ------------------------------------------------------------------*/

#include "edf_stream_mgr.h"
#include "edf_base.h"

/* Private constants ---------------------------------------------------------*/

/* Private types -------------------------------------------------------------*/

/* Private variables ---------------------------------------------------------*/

/* Private defines -----------------------------------------------------------*/

/**
 * @brief 索引取模（仅用于 2 的幂容量）
 */
#define STREAM_IDX(s, i)             ((i) & ((s)->capacity - 1U))

/**
 * @brief 检查容量是否为 2 的幂（内部索引计算依赖此约束）
 */
#define STREAM_CAP_IS_POW2(n)        (((n) != 0) && (((n) & ((n) - 1)) == 0))

/**
 * @brief 环形缓冲区地址对齐要求（字节）
 */
#define STREAM_BUF_ALIGN_SIZE        4U

/**
 * @brief 检查缓冲区地址是否满足对齐要求（未对齐会破坏 DMA 访问）
 */
#define STREAM_IS_ADDR_ALIGNED(a)    (((uintptr_t)(a) & (STREAM_BUF_ALIGN_SIZE - 1U)) == 0U)

/* Private function prototypes ----------------------------------------------*/

/* Private functions ---------------------------------------------------------*/

/* Exported constants --------------------------------------------------------*/

/* Exported variables --------------------------------------------------------*/

/* Exported functions --------------------------------------------------------*/

/**
 * @brief 初始化字节流（静态创建）
 */
stream_t *stream_init(stream_t *stream,
                      uint8_t *buffer,
                      hal_size_t capacity,
                      task_t *notify_task,
                      task_event_t notify_event)
{
    /* 参数检查 */
    if (stream == (stream_t *)NULL || 
		buffer == (uint8_t *)NULL || 
        capacity == 0 || 
		!STREAM_CAP_IS_POW2(capacity) || 
		!STREAM_IS_ADDR_ALIGNED(buffer) || 
		stream->inited != 0)
    {
        return (stream_t *)NULL;
    }

    /* 初始化控制块 */
    stream->buffer 		  = buffer;
    stream->capacity      = capacity;
    stream->head          = 0;
    stream->tail          = 0;
    stream->count         = 0;
    stream->notify_task   = notify_task;
    stream->notify_event  = notify_event;
    stream->inited        = 1;

    return stream;
}

/**
 * @brief 写入字节（部分写入语义）
 */
edf_status_t stream_write(stream_t *stream,
                          const void *data,
                          hal_size_t len,
                          hal_size_t *written)
{
    const uint8_t *src;
    hal_size_t free_size;
    hal_size_t first_chunk;
    hal_size_t second_chunk;
    uint8_t notify = 0;
	hal_size_t idx;

    /* 参数检查 */
    if (stream == (stream_t *)NULL || 
		stream->inited == 0 || 
		written == (hal_size_t *)NULL || 
		(data == (const void *)NULL && len != 0))
    {
        return EDF_ERR;
    }

    /* 长度为 0 视为合法空操作 */
    if (len == 0)
    {
        *written = 0;
        return EDF_OK;
    }

    src = (const uint8_t *)data;

    HAL_ENTER_CRITICAL_SECTION();

    free_size = stream->capacity - stream->count;

    /* 剩余空间不足：截断写入，实际写入量由 written 返回 */
    if (len > free_size)
    {
        len = free_size;
    }

    if (len > 0)
    {
        /* 处理环形回绕：先写 tail 到 buffer 末尾，再从头写剩余 */
        first_chunk  = stream->capacity - STREAM_IDX(stream, stream->tail);
        if (first_chunk > len)
        {
            first_chunk = len;
        }
        second_chunk = len - first_chunk;

        if (first_chunk > 0)
        {
			idx = STREAM_IDX(stream, stream->tail);
            for (hal_size_t i = 0; i < first_chunk; i++)
            {
                stream->buffer[idx + i] = src[i];
            }
        }
        if (second_chunk > 0)
        {
            for (hal_size_t i = 0; i < second_chunk; i++)
            {
                stream->buffer[i] = src[first_chunk + i];
            }
        }

        stream->tail += len;
        stream->count += len;
        notify = 1;
    }

    HAL_EXIT_CRITICAL_SECTION();

    *written = len;

    /* 临界区外触发通知，避免临界区内做过长操作 */
    if (notify && stream->notify_task != (task_t *)NULL)
    {
        task_set_event(stream->notify_task, stream->notify_event);
    }

    return EDF_OK;
}

/**
 * @brief 从字节流读取字节（消费）
 */
edf_status_t stream_read(stream_t *stream,
                         void *out,
                         hal_size_t len,
                         hal_size_t *readed)
{
    edf_status_t ret;

    if (stream == (stream_t *)NULL || 
		stream->inited == 0 || 
		readed == (hal_size_t *)NULL || 
		(out == (void *)NULL && len != 0))
    {
        return EDF_ERR;
    }

    if (len == 0)
    {
        *readed = 0;
        return EDF_OK;
    }

    ret = stream_peek(stream, out, len, readed);

    if (ret == EDF_OK && *readed > 0)
    {
        HAL_ENTER_CRITICAL_SECTION();

        stream->head  += *readed;
        stream->count -= *readed;

        HAL_EXIT_CRITICAL_SECTION();
    }

    return ret;
}

/**
 * @brief 获取读端连续可读区域指针（零拷贝）
 */
const uint8_t *stream_get_read_ptr(stream_t *stream, hal_size_t *contig_len)
{
    const uint8_t *ptr;
    hal_size_t first_chunk;

    if (stream == (stream_t *)NULL || 
		stream->inited == 0)
    {
        if (contig_len != (hal_size_t *)NULL)
        {
            *contig_len = 0;
        }
        return (const uint8_t *)NULL;
    }

    HAL_ENTER_CRITICAL_SECTION();

    if (stream->count == 0)
    {
        first_chunk = 0;
        ptr         = (const uint8_t *)NULL;
    }
    else
    {
        /* 取 head 到 buffer 末尾的连续长度，受 count 约束 */
        first_chunk = stream->capacity - STREAM_IDX(stream, stream->head);
        if (first_chunk > stream->count)
        {
            first_chunk = stream->count;
        }
        ptr = &stream->buffer[STREAM_IDX(stream, stream->head)];
    }

    HAL_EXIT_CRITICAL_SECTION();

    if (contig_len != (hal_size_t *)NULL)
    {
        *contig_len = first_chunk;
    }

    return ptr;
}

/**
 * @brief 获取写端连续可写区域指针（零拷贝）
 */
uint8_t *stream_get_write_ptr(stream_t *stream, hal_size_t *contig_len)
{
    uint8_t *ptr;
    hal_size_t free_size;
    hal_size_t first_chunk;

    if (stream == (stream_t *)NULL || 
		stream->inited == 0)
    {
        if (contig_len != (hal_size_t *)NULL)
        {
            *contig_len = 0;
        }
        return (uint8_t *)NULL;
    }

    HAL_ENTER_CRITICAL_SECTION();

    free_size = stream->capacity - stream->count;
    if (free_size == 0)
    {
        first_chunk = 0;
        ptr         = (uint8_t *)NULL;
    }
    else
    {
        /* 取 tail 到 buffer 末尾的连续长度，受 free_size 约束 */
        first_chunk = stream->capacity - STREAM_IDX(stream, stream->tail);
        if (first_chunk > free_size)
        {
            first_chunk = free_size;
        }
        ptr = &stream->buffer[STREAM_IDX(stream, stream->tail)];
    }

    HAL_EXIT_CRITICAL_SECTION();

    if (contig_len != (hal_size_t *)NULL)
    {
        *contig_len = first_chunk;
    }

    return ptr;
}

/**
 * @brief 推进读索引（消费若干字节）
 */
edf_status_t stream_advance_read(stream_t *stream, hal_size_t len)
{
    hal_size_t avail;

    if (stream == (stream_t *)NULL || 
		stream->inited == 0)
    {
        return EDF_ERR;
    }

    HAL_ENTER_CRITICAL_SECTION();

    avail = stream->count;
    if (len > avail)
    {
        len = avail;
    }

    stream->head  += len;
    stream->count -= len;

    HAL_EXIT_CRITICAL_SECTION();

    return EDF_OK;
}

/**
 * @brief 推进写索引（提交若干字节）
 */
edf_status_t stream_advance_write(stream_t *stream, hal_size_t len)
{
    hal_size_t free_size;
    uint8_t notify = 0;
    edf_status_t ret = EDF_OK;

    if (stream == (stream_t *)NULL || 
		stream->inited == 0)
    {
        return EDF_ERR;
    }

    HAL_ENTER_CRITICAL_SECTION();

    free_size = stream->capacity - stream->count;

    /* 剩余空间不足：只提交能容纳的部分，并用返回值告知发生截断 */
    if (len > free_size)
    {
        len = free_size;
        ret = EDF_ERR;
    }

    if (len > 0)
    {
        stream->tail  += len;
        stream->count += len;
        notify = 1;
    }

    HAL_EXIT_CRITICAL_SECTION();

    /* 临界区外触发通知 */
    if (notify && stream->notify_task != (task_t *)NULL)
    {
        task_set_event(stream->notify_task, stream->notify_event);
    }

    return ret;
}

/**
 * @brief 窥视字节（不消费）
 */
edf_status_t stream_peek(stream_t *stream,
                         void *out,
                         hal_size_t len,
                         hal_size_t *peeked)
{
    uint8_t *dst;
    hal_size_t avail;
    hal_size_t first_chunk;
    hal_size_t second_chunk;
	hal_size_t idx;

    if (stream == (stream_t *)NULL || 
		stream->inited == 0 || 
		peeked == (hal_size_t *)NULL || 
		(out == (void *)NULL && len != 0))
    {
        return EDF_ERR;
    }

    if (len == 0)
    {
        *peeked = 0;
        return EDF_OK;
    }

    dst = (uint8_t *)out;

    HAL_ENTER_CRITICAL_SECTION();

    avail = stream->count;
    if (len > avail)
    {
        len = avail;
    }

    if (len > 0)
    {
        /* 处理环形回绕 */
        first_chunk  = stream->capacity - STREAM_IDX(stream, stream->head);
        if (first_chunk > len)
        {
            first_chunk = len;
        }
        second_chunk = len - first_chunk;

        if (first_chunk > 0)
        {
            idx = STREAM_IDX(stream, stream->head);
            for (hal_size_t i = 0; i < first_chunk; i++)
            {
                dst[i] = stream->buffer[idx + i];
            }
        }
        if (second_chunk > 0)
        {
            for (hal_size_t i = 0; i < second_chunk; i++)
            {
                dst[first_chunk + i] = stream->buffer[i];
            }
        }
    }

    HAL_EXIT_CRITICAL_SECTION();

    *peeked = len;

    return EDF_OK;
}

/**
 * @brief 获取已用字节数
 */
hal_size_t stream_get_used(stream_t *stream)
{
    if (stream == (stream_t *)NULL || 
		stream->inited == 0)
    {
        return 0;
    }

    return stream->count;
}

/**
 * @brief 清空字节流
 */
edf_status_t stream_flush(stream_t *stream)
{
    if (stream == (stream_t *)NULL || 
		stream->inited == 0)
    {
        return EDF_ERR;
    }

    HAL_ENTER_CRITICAL_SECTION();

    stream->head  = 0;
    stream->tail  = 0;
    stream->count = 0;

    HAL_EXIT_CRITICAL_SECTION();

    return EDF_OK;
}

/* Private functions --------------------------------------------------------*/
