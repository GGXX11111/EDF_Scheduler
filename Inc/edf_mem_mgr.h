/**
 ******************************************************************************
 * @file    edf_mem_mgr.h
 * @brief   内存管理器接口
 ******************************************************************************
 */

#ifndef EDF_MEM_MGR_H
#define EDF_MEM_MGR_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/

#include "edf_base.h"

/* Exported constants --------------------------------------------------------*/

/* Exported types ------------------------------------------------------------*/

/* Exported variables --------------------------------------------------------*/

/* Exported defines ----------------------------------------------------------*/

/* Exported functions --------------------------------------------------------*/

/******************************************************************************/
/*                        内存管理函数                                        */
/******************************************************************************/

/**
 * @brief 分配内存块
 * @param size 要分配的内存块大小（字节）
 * @return 成功返回指向内存块的指针，失败返回NULL
 * @note 分配的内存未初始化，包含随机值
 * @note 禁止在中断上下文调用
 */
void *mem_malloc(hal_size_t size);

/**
 * @brief 分配并清零内存块
 * @param num  要分配的元素个数
 * @param size 每个元素的大小（字节）
 * @return 成功返回指向内存块的指针，失败返回NULL
 * @note 分配的内存会初始化为0
 * @note 禁止在中断上下文调用
 */
void *mem_calloc(hal_size_t num, hal_size_t size);

/**
 * @brief 重新分配内存块
 * @param ptr 原内存块指针（可为NULL，等效于malloc）
 * @param size 新的内存块大小（字节；为0时等效于free并返回NULL）
 * @return 成功返回指向新内存块的指针，失败返回NULL
 * @note 缩小时直接截断并释放多余空间，扩大时分配新块并复制旧数据
 * @note 禁止在中断上下文调用
 */
void *mem_realloc(void *ptr, hal_size_t size);

/**
 * @brief 释放内存块
 * @param ptr 要释放的内存块指针（可为NULL，NULL时为空操作）
 * @note ptr必须是通过mem_*函数分配的
 * @note 重复释放会被is_free检查拦截并安全返回
 * @note 禁止在中断上下文调用
 */
void mem_free(void *ptr);

#if MEM_STATS_ENABLE
/**
 * @brief 获取当前已用内存大小（字节，含块头开销）
 * @return 已用内存字节数（含 mem_blk_t 头部大小）
 */
hal_size_t mem_get_used_size(void);
#endif

#ifdef __cplusplus
}
#endif

#endif /* EDF_MEM_MGR_H */
