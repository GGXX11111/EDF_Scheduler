/**
 ******************************************************************************
 * @file    edf_stream_mgr.c
 * @brief   字节流管理器实现（基于环形缓冲的字节流通道）
 ******************************************************************************
 */

/* Includes ------------------------------------------------------------------*/

#include "edf_stream_mgr.h"
#include "edf_base.h"
#include "edf_mem_mgr.h"

/* Private constants ---------------------------------------------------------*/

/* Private types -------------------------------------------------------------*/

/* 字节流控制块 */
struct stream
{
    uint8_t      *buffer;         // 环形缓冲（创建时分配，按系统对齐）
    hal_size_t    capacity;       // 容量（必须为 2 的幂）
    hal_size_t    head;           // 读索引（单调递增，取模用 & (cap-1)）
    hal_size_t    tail;           // 写索引（单调递增，取模用 & (cap-1)）
    hal_size_t    count;          // 当前已用字节数
    uint8_t       overflow;       // 溢出标志：写满被截断时置位
    uint8_t       reserved[3];
    task_t       *notify_task;    // 数据到达时通知的任务
    task_event_t  notify_event;   // 数据到达时触发的事件标志
};

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

/* Private function prototypes ----------------------------------------------*/

/* Private functions ---------------------------------------------------------*/

/* Exported constants --------------------------------------------------------*/

/* Exported variables --------------------------------------------------------*/

/* Exported functions --------------------------------------------------------*/

/**
 * @brief 创建字节流
 */
stream_t *stream_create(hal_size_t capacity,
                        task_t *notify_task,
                        task_event_t notify_event)
{
    stream_t *new_stream;
    hal_size_t alloc_size;

    /* 参数检查：容量必须为 2 的幂（内部索引取模依赖此约束） */
    if (capacity == 0 || !STREAM_CAP_IS_POW2(capacity))
    {
        return (stream_t *)NULL;
    }

    /* 分配控制块 */
    new_stream = (stream_t *)mem_malloc(sizeof(stream_t));
    if (new_stream == (stream_t *)NULL)
    {
        return (stream_t *)NULL;
    }

    /* 分配环形缓冲并按系统对齐（便于后续 DMA） */
    alloc_size = SYS_ALIGN_UP(capacity);
    new_stream->buffer = (uint8_t *)mem_malloc(alloc_size);
    if (new_stream->buffer == (uint8_t *)NULL)
    {
        mem_free(new_stream);
        return (stream_t *)NULL;
    }

    /* 初始化控制块 */
    new_stream->capacity      = capacity;
    new_stream->head          = 0;
    new_stream->tail          = 0;
    new_stream->count         = 0;
    new_stream->overflow      = 0;
    new_stream->notify_task   = notify_task;
    new_stream->notify_event  = notify_event;

    return new_stream;
}

/**
 * @brief 删除字节流
 */
void stream_delete(stream_t *stream)
{
    /* 参数检查 */
    if (stream == (stream_t *)NULL)
    {
        return;
    }

    /* 释放环形缓冲和控制块 */
    mem_free(stream->buffer);
    mem_free(stream);
}

/**
 * @brief 写入字节（部分写入语义）
 */
uint8_t stream_write(stream_t *stream,
                     const void *data,
                     hal_size_t len,
                     hal_size_t *written)
{
    const uint8_t *src;
    hal_size_t free_size;
    hal_size_t first_chunk;
    hal_size_t second_chunk;
    uint8_t notify = 0;

    /* 参数检查 */
    if (stream == (stream_t *)NULL || (data == (const void *)NULL && len != 0))
    {
        return EDF_FAILURE;
    }

    /* 长度为 0 视为合法空操作 */
    if (len == 0)
    {
        if (written != (hal_size_t *)NULL)
        {
            *written = 0;
        }
        return EDF_SUCCESS;
    }

    src = (const uint8_t *)data;

    HAL_ENTER_CRITICAL_SECTION();

    free_size = stream->capacity - stream->count;

    /* 剩余空间不足：截断写入并置位溢出标志 */
    if (len > free_size)
    {
        stream->overflow = 1;
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
            for (hal_size_t i = 0; i < first_chunk; i++)
            {
                stream->buffer[STREAM_IDX(stream, stream->tail) + i] = src[i];
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

    if (written != (hal_size_t *)NULL)
    {
        *written = len;
    }

    /* 临界区外触发通知，避免临界区内做过长操作 */
    if (notify && stream->notify_task != (task_t *)NULL)
    {
        task_set_event(stream->notify_task, stream->notify_event);
    }

    return EDF_SUCCESS;
}

/**
 * @brief 从字节流读取字节（消费）
 */
uint8_t stream_read(stream_t *stream,
                    void *out,
                    hal_size_t len,
                    hal_size_t *readed)
{
    uint8_t ret;

    if (stream == (stream_t *)NULL || (out == (void *)NULL && len != 0))
    {
        return EDF_FAILURE;
    }

    if (len == 0)
    {
        if (readed != (hal_size_t *)NULL)
        {
            *readed = 0;
        }
        return EDF_SUCCESS;
    }

    ret = stream_peek(stream, out, len, readed);

    if (ret == EDF_SUCCESS && readed != (hal_size_t *)NULL && *readed > 0)
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

    if (stream == (stream_t *)NULL)
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

    if (stream == (stream_t *)NULL)
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
uint8_t stream_advance_read(stream_t *stream, hal_size_t len)
{
    hal_size_t avail;

    if (stream == (stream_t *)NULL)
    {
        return EDF_FAILURE;
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

    return EDF_SUCCESS;
}

/**
 * @brief 推进写索引（提交若干字节）
 */
uint8_t stream_advance_write(stream_t *stream, hal_size_t len)
{
    hal_size_t free_size;
    uint8_t notify = 0;

    if (stream == (stream_t *)NULL)
    {
        return EDF_FAILURE;
    }

    HAL_ENTER_CRITICAL_SECTION();

    free_size = stream->capacity - stream->count;

    /* 剩余空间不足：截断提交并置位溢出标志 */
    if (len > free_size)
    {
        stream->overflow = 1;
        len = free_size;
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

    return EDF_SUCCESS;
}

/**
 * @brief 窥视字节（不消费）
 */
uint8_t stream_peek(stream_t *stream,
                    void *out,
                    hal_size_t len,
                    hal_size_t *peeked)
{
    uint8_t *dst;
    hal_size_t avail;
    hal_size_t first_chunk;
    hal_size_t second_chunk;

    if (stream == (stream_t *)NULL || (out == (void *)NULL && len != 0))
    {
        return EDF_FAILURE;
    }

    if (len == 0)
    {
        if (peeked != (hal_size_t *)NULL)
        {
            *peeked = 0;
        }
        return EDF_SUCCESS;
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
            for (hal_size_t i = 0; i < first_chunk; i++)
            {
                dst[i] = stream->buffer[STREAM_IDX(stream, stream->head) + i];
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

    if (peeked != (hal_size_t *)NULL)
    {
        *peeked = len;
    }

    return EDF_SUCCESS;
}

/**
 * @brief 获取已用字节数
 */
hal_size_t stream_get_used(stream_t *stream)
{
    if (stream == (stream_t *)NULL)
    {
        return 0;
    }

    return stream->count;
}

/**
 * @brief 获取剩余字节数
 */
hal_size_t stream_get_free(stream_t *stream)
{
    if (stream == (stream_t *)NULL)
    {
        return 0;
    }

    return stream->capacity - stream->count;
}

/**
 * @brief 判断字节流是否为空
 */
uint8_t stream_is_empty(stream_t *stream)
{
    if (stream == (stream_t *)NULL)
    {
        return EDF_FAILURE;
    }

    return (stream->count == 0) ? EDF_SUCCESS : EDF_FAILURE;
}

/**
 * @brief 判断字节流是否已满
 */
uint8_t stream_is_full(stream_t *stream)
{
    if (stream == (stream_t *)NULL)
    {
        return EDF_FAILURE;
    }

    return (stream->count >= stream->capacity) ? EDF_SUCCESS : EDF_FAILURE;
}

/**
 * @brief 检查并清除溢出标志
 */
uint8_t stream_check_overflow(stream_t *stream)
{
    uint8_t result;

    if (stream == (stream_t *)NULL)
    {
        return EDF_FAILURE;
    }

    HAL_ENTER_CRITICAL_SECTION();

    result = (stream->overflow != 0) ? EDF_SUCCESS : EDF_FAILURE;
    stream->overflow = 0;

    HAL_EXIT_CRITICAL_SECTION();

    return result;
}

/**
 * @brief 丢弃若干字节
 */
uint8_t stream_skip(stream_t *stream, hal_size_t len)
{
    hal_size_t avail;

    if (stream == (stream_t *)NULL)
    {
        return EDF_FAILURE;
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

    return EDF_SUCCESS;
}

/**
 * @brief 清空字节流
 */
uint8_t stream_flush(stream_t *stream)
{
    if (stream == (stream_t *)NULL)
    {
        return EDF_FAILURE;
    }

    HAL_ENTER_CRITICAL_SECTION();

    stream->head     = 0;
    stream->tail     = 0;
    stream->count    = 0;
    stream->overflow = 0;

    HAL_EXIT_CRITICAL_SECTION();

    return EDF_SUCCESS;
}

/* Private functions --------------------------------------------------------*/
