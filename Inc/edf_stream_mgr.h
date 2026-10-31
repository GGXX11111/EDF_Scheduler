/**
 ******************************************************************************
 * @file    edf_stream_mgr.h
 * @brief   字节流管理器接口
 ******************************************************************************
 */

#ifndef EDF_STREAM_MGR_H
#define EDF_STREAM_MGR_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/

#include "edf_base.h"
#include "edf_task_mgr.h"

/* Exported types ------------------------------------------------------------*/

/* 字节流句柄类型 */
typedef struct stream stream_t;

/**
 * @brief 字节流控制块
 */
struct stream
{
    uint8_t *buffer;          		// 环形缓冲（由调用者提供，长度 >= capacity）
    hal_size_t capacity;        	// 最大可容纳的字节数（必须为 2 的幂）
    volatile hal_size_t head;  		// 读索引
    volatile hal_size_t tail;  		// 写索引
    volatile hal_size_t count; 		// 当前已用字节数
    uint8_t inited;    				// 初始化标志
    uint8_t reserved0[3];
    task_t *notify_task;     		// 数据到达时通知的任务
    task_event_t notify_event;		// 数据到达时触发的事件标志
} ;

/* Exported defines ----------------------------------------------------------*/

/**
 * @brief 定义字节流控制块，零初始化
 */
#define STREAM_DEFINE(stream)	stream_t stream = {0}

/* Exported functions --------------------------------------------------------*/

/**
 * @brief 初始化字节流（静态创建）
 * @param stream       调用者提供的字节流控制块，必须零初始化
 * @param buffer       调用者提供的环形缓冲，长度必须 >= capacity，必须 4 字节对齐
 * @param capacity     容量（字节），必须为 2 的幂
 * @param notify_task  数据到达时通知的任务句柄，可为NULL
 * @param notify_event 数据到达时触发的事件标志，仅在 notify_task 不为NULL时有效
 * @return 成功返回 stream，参数无效或重复初始化返回NULL
 * @note 初始化后长期存在，没有对应的销毁接口；
 * @note 禁止在中断上下文调用
 */
stream_t *stream_init(stream_t *stream,
                      uint8_t *buffer,
                      hal_size_t capacity,
                      task_t *notify_task,
                      task_event_t notify_event);

/**
 * @brief 写入字节到字节流
 * @param stream  字节流句柄
 * @param data    待写入数据指针
 * @param len     请求写入字节数
 * @param written 实际写入字节数（返回值）
 * @return 成功返回EDF_OK，失败返回EDF_ERR
 * @note 剩余空间不足时只写入能容纳的部分，实际写入量由 written 返回
 * @note len == 0 时为合法空操作，返回 EDF_OK 且 *written = 0
 * @note 写入成功后若绑定了通知任务，会设置该任务的 notify_event
 * @note 允许在中断上下文调用
 */
edf_status_t stream_write(stream_t *stream,
                          const void *data,
                          hal_size_t len,
                          hal_size_t *written);

/**
 * @brief 从字节流读取字节
 * @param stream 字节流句柄
 * @param out    数据输出缓冲区
 * @param len    请求读取字节数
 * @param readed 实际读取字节数（返回值）
 * @return 成功返回EDF_OK，失败返回EDF_ERR
 * @note 缓冲不足时只读取已有部分，实际读取量由 readed 返回
 * @note len == 0 时为合法空操作，返回 EDF_OK 且 *readed = 0
 * @note 允许在中断上下文调用
 */
edf_status_t stream_read(stream_t *stream,
                         void *out,
                         hal_size_t len,
                         hal_size_t *readed);

/**
 * @brief 获取写端当前连续可写区域指针（零拷贝）
 * @param stream     字节流句柄
 * @param contig_len 连续可写字节数（输出），字节流已满时为 0
 * @return 连续可写区域起始指针；字节流已满或参数无效返回 NULL
 * @note 当剩余空间跨越 buffer 末尾时，仅返回 tail 到 buffer 末尾的一段
 * @note 若需要写入 wrap 之后的空间，请先 stream_advance_write()，再调用本函数
 * @note 允许在中断上下文调用
 */
uint8_t *stream_get_write_ptr(stream_t *stream, hal_size_t *contig_len);

/**
 * @brief 获取读端当前连续可读区域指针（零拷贝）
 * @param stream     字节流句柄
 * @param contig_len 连续可读字节数（输出），字节流为空时为 0
 * @return 连续可读区域起始指针；字节流为空或参数无效返回 NULL
 * @note 当数据跨越 buffer 末尾时，仅返回 head 到 buffer 末尾的一段
 * @note 若需要读取 wrap 之后的数据，请先 stream_advance_read()，再调用本函数
 * @note 允许在中断上下文调用
 */
const uint8_t *stream_get_read_ptr(stream_t *stream, hal_size_t *contig_len);

/**
 * @brief 推进写索引（提交若干字节）
 * @param stream 字节流句柄
 * @param len    请求推进字节数
 * @return 成功返回EDF_OK，失败返回EDF_ERR
 * @note len 大于剩余空间时仅提交能容纳的部分，并返回 EDF_ERR
 * @note 配合 stream_get_write_ptr() 使用
 * @note 推进后若绑定了通知任务，会设置该任务的 notify_event
 * @note 允许在中断上下文调用
 */
edf_status_t stream_advance_write(stream_t *stream, hal_size_t len);

/**
 * @brief 推进读索引（消费若干字节）
 * @param stream 字节流句柄
 * @param len    请求推进字节数
 * @return 成功返回EDF_OK，失败返回EDF_ERR
 * @note len 大于已用字节数时仅推进到 head == tail，相当于清空，返回 EDF_OK
 * @note 配合 stream_get_read_ptr() 使用
 * @note 允许在中断上下文调用
 */
edf_status_t stream_advance_read(stream_t *stream, hal_size_t len);

/**
 * @brief 从字节流窥视字节（不消费）
 * @param stream 字节流句柄
 * @param out    数据输出缓冲区
 * @param len    请求窥视字节数
 * @param peeked 实际窥视字节数（返回值）
 * @return 成功返回EDF_OK，失败返回EDF_ERR
 * @note 与 stream_read 行为一致，但不移动读索引、不更新计数
 * @note 允许在中断上下文调用
 */
edf_status_t stream_peek(stream_t *stream,
                         void *out,
                         hal_size_t len,
                         hal_size_t *peeked);

/**
 * @brief 获取已用字节数
 * @param stream 字节流句柄
 * @return 当前已用字节数，参数无效返回 0
 * @note 允许在中断上下文调用
 */
hal_size_t stream_get_used(stream_t *stream);

/**
 * @brief 清空字节流
 * @param stream 字节流句柄
 * @return 成功返回EDF_OK，失败返回EDF_ERR
 * @note 允许在中断上下文调用
 */
edf_status_t stream_flush(stream_t *stream);

#ifdef __cplusplus
}
#endif

#endif /* EDF_STREAM_MGR_H */
