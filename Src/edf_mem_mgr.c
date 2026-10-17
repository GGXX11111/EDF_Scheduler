/**
 ******************************************************************************
 * @file    edf_mem_mgr.c
 * @brief   内存管理器实现
 ******************************************************************************
 */

/* Includes ------------------------------------------------------------------*/

#include "edf_mem_mgr.h"
#include "edf_base.h"

/* Private constants ---------------------------------------------------------*/

#define HAL_SIZE_MAX            ((hal_size_t)(-1))      				// 分割块大小最大值
#define MEM_MIN_SPLIT_SIZE      32                      				// 最小分割块大小
#define MEM_BLOCK_HDR_SIZE      sizeof(mem_blk_t)       				// 内存块头部大小
#define MEM_POOL_USER_SIZE      (MEM_POOL_SIZE - MEM_BLOCK_HDR_SIZE)   	// 内存池用户数据区大小

/* Private types -------------------------------------------------------------*/

/* 内存块控制头 */
typedef struct mem_blk
{
    struct mem_blk *next;                   // 指向下一个空闲块的指针
    struct mem_blk *prev;                   // 指向前一个空闲块的指针
    hal_size_t size;                        // 块大小（包括头部）
    hal_size_t is_free;                     // 是否空闲：1-空闲，0-占用
} mem_blk_t;

/* 内存池结构体 */
typedef struct mem_pool
{
    mem_blk_t hdr;                          // 空闲块头
    uint8_t data[MEM_POOL_USER_SIZE];       // 用户数据区
} mem_pool_t;

#if MEM_STATS_ENABLE
/* 内存统计信息结构体 */
typedef struct mem_stats
{
    hal_size_t total_mem;                   // 总内存（字节）
    hal_size_t used_mem;                    // 已用内存（字节）
    hal_size_t free_mem;                    // 空闲内存（字节）
    hal_size_t alloc_count;                 // 分配次数
    hal_size_t free_count;                  // 释放次数
    hal_size_t alloc_failures;              // 分配失败次数
} mem_stats_t;
#endif

/* Private variables ---------------------------------------------------------*/

/* 内存池静态内存区域 */
static mem_pool_t g_mem_pool =
{
    .hdr =
    {
        .next     = (mem_blk_t*)NULL,
        .prev     = (mem_blk_t*)NULL,
        .size     = MEM_POOL_SIZE,
        .is_free  = 1
    }
};

/* 空闲块链表头指针 */
static mem_blk_t *g_free_list = &g_mem_pool.hdr;

#if MEM_STATS_ENABLE
/* 内存统计信息 */
static mem_stats_t g_mem_stats =
{
    .total_mem      = MEM_POOL_SIZE,
    .free_mem       = MEM_POOL_SIZE,
    .used_mem       = 0,
    .alloc_count    = 0,
    .free_count     = 0,
    .alloc_failures = 0
};
#endif

/* Private defines -----------------------------------------------------------*/

/* Private function prototypes ----------------------------------------------*/

/* 静态函数声明 */
static void *mem_set(void *dest, uint8_t value, hal_size_t count);
static void *mem_copy(void *dest, const void *src, hal_size_t count);
static mem_blk_t *find_free_block(hal_size_t size);
static mem_blk_t *split_block(mem_blk_t *blk, hal_size_t size);
static void merge_adjacent_blocks(mem_blk_t *blk);
static void insert_to_free_list(mem_blk_t *blk);
static void remove_from_free_list(mem_blk_t *blk);

/* Exported constants --------------------------------------------------------*/

/* Exported variables --------------------------------------------------------*/

/* Exported functions --------------------------------------------------------*/

/**
 * @brief 分配内存块（malloc）
 */
void *mem_malloc(hal_size_t size)
{
    mem_blk_t *blk;
    hal_size_t total_size;

    if (size == 0)
    {
        return (void*)NULL;
    }

    /* 防止 size + 头部大小 整数溢出 */
    if (size > HAL_SIZE_MAX - MEM_BLOCK_HDR_SIZE)
    {
        return (void*)NULL;
    }

    /* 计算总大小（包括头部），使用系统对齐宏 */
    total_size = SYS_ALIGN_UP(size + MEM_BLOCK_HDR_SIZE);

    HAL_ENTER_CRITICAL_SECTION();

    /* 查找合适的空闲块 */
    blk = find_free_block(total_size);

    if (blk == (mem_blk_t*)NULL)
    {
#if MEM_STATS_ENABLE
        g_mem_stats.alloc_failures++;
#endif

        HAL_EXIT_CRITICAL_SECTION();

        return (void*)NULL;
    }

    /* 分割并分配块 */
    blk = split_block(blk, total_size);

#if MEM_STATS_ENABLE
    g_mem_stats.used_mem += blk->size;
    g_mem_stats.free_mem -= blk->size;
    g_mem_stats.alloc_count++;
#endif

    HAL_EXIT_CRITICAL_SECTION();

    /* 返回用户区域（跳过头部） */
    return (void*)((uint8_t*)blk + MEM_BLOCK_HDR_SIZE);
}

/**
 * @brief 分配并清零内存块（calloc）
 */
void *mem_calloc(hal_size_t num, hal_size_t size)
{
    void *ptr;
    hal_size_t total_size;

    /* 检查溢出 */
    if (num != 0 && size > HAL_SIZE_MAX / num)
    {
        return (void*)NULL;
    }

    total_size = num * size;

    ptr = mem_malloc(total_size);

    if (ptr != (void*)NULL)
    {
        /* 清零内存 */
        mem_set(ptr, 0, total_size);
    }

    return ptr;
}

/**
 * @brief 重新分配内存块（realloc）
 */
void *mem_realloc(void *ptr, hal_size_t size)
{
    mem_blk_t *old_blk;
    mem_blk_t *new_blk;
    mem_blk_t *split_blk;
    hal_size_t old_user_size;
    hal_size_t old_blk_size;
    hal_size_t new_user_size;
    hal_size_t copy_size;
    hal_size_t total_size;
    hal_size_t remaining_size;
    void *new_ptr;

    /* 如果ptr为NULL，等效于malloc */
    if (ptr == (void*)NULL)
    {
        return mem_malloc(size);
    }

    /* 如果size为0，等效于free */
    if (size == 0)
    {
        mem_free(ptr);
        return (void*)NULL;
    }

    /* 获取旧块信息 */
    old_blk = (mem_blk_t*)((uint8_t*)ptr - MEM_BLOCK_HDR_SIZE);

    HAL_ENTER_CRITICAL_SECTION();

    /* 验证块是否处于已分配状态 */
    if (old_blk->is_free == 1)
    {
        HAL_EXIT_CRITICAL_SECTION();

        return (void*)NULL;
    }

    old_user_size = old_blk->size - MEM_BLOCK_HDR_SIZE;
    old_blk_size = old_blk->size;

    HAL_EXIT_CRITICAL_SECTION();

    /* 计算新大小（对齐后，含头部） */
    if (size > HAL_SIZE_MAX - MEM_BLOCK_HDR_SIZE)
    {
        return (void*)NULL;
    }

    total_size = SYS_ALIGN_UP(size + MEM_BLOCK_HDR_SIZE);

    /* 如果新大小等于旧大小，直接返回 */
    if (total_size == old_blk_size)
    {
        return ptr;
    }

    /* 如果新大小小于旧大小，尝试分割释放多余空间 */
    if (total_size < old_blk_size)
    {
		HAL_ENTER_CRITICAL_SECTION();

        remaining_size = old_blk->size - total_size;

        /* 剩余空间足够分割才分割，否则保持原样避免碎片 */
        if (remaining_size >= MEM_MIN_SPLIT_SIZE + MEM_BLOCK_HDR_SIZE &&
            SYS_IS_ADDR_ALIGNED((uint8_t*)old_blk + total_size))
        {
            /* 缩小原块 */
            old_blk->size = total_size;

            /* 创建新的空闲块并插入空闲链表 */
            split_blk = (mem_blk_t*)((uint8_t*)old_blk + total_size);
            split_blk->size = remaining_size;
            split_blk->is_free = 1;
            split_blk->next = (mem_blk_t*)NULL;
            split_blk->prev = (mem_blk_t*)NULL;

            insert_to_free_list(split_blk);

#if MEM_STATS_ENABLE
            g_mem_stats.used_mem -= remaining_size;
            g_mem_stats.free_mem += remaining_size;
#endif
        }

		HAL_EXIT_CRITICAL_SECTION();

        return ptr;
    }

    /* 新大小大于旧大小，分配新块并复制 */
    if (total_size > old_blk_size)
    {
		/* 分配新块 */
        new_ptr = mem_malloc(size);
        if (new_ptr == (void*)NULL)
        {
            return (void*)NULL;
        }

        /* 复制旧数据 */
        new_blk = (mem_blk_t*)((uint8_t*)new_ptr - MEM_BLOCK_HDR_SIZE);
        new_user_size = new_blk->size - MEM_BLOCK_HDR_SIZE;
        copy_size = (old_user_size < new_user_size) ? old_user_size : new_user_size;
        mem_copy(new_ptr, ptr, copy_size);

        /* 释放旧块 */
        mem_free(ptr);

        return new_ptr;
    }

    return (void*)NULL;
}

/**
 * @brief 释放内存块
 */
void mem_free(void *ptr)
{
    mem_blk_t *blk;

    if (ptr == (void*)NULL)
    {
        return;
    }

    HAL_ENTER_CRITICAL_SECTION();

    blk = (mem_blk_t*)((uint8_t*)ptr - MEM_BLOCK_HDR_SIZE);

    /* 验证块的有效性 */
    if (blk->is_free == 1)
    {
        HAL_EXIT_CRITICAL_SECTION();

        return;
    }

#if MEM_STATS_ENABLE
    g_mem_stats.used_mem -= blk->size;
    g_mem_stats.free_mem += blk->size;
    g_mem_stats.free_count++;
#endif

    /* 将块插入到空闲链表并合并 */
    insert_to_free_list(blk);

    HAL_EXIT_CRITICAL_SECTION();
}

#if MEM_STATS_ENABLE
/**
 * @brief 获取当前已用内存大小（字节）
 * @return 已用内存字节数
 */
hal_size_t mem_get_used_size(void)
{
    hal_size_t used_mem;

    HAL_ENTER_CRITICAL_SECTION();
	
    used_mem = g_mem_stats.used_mem;
	
    HAL_EXIT_CRITICAL_SECTION();

    return used_mem;
}
#endif

/* Private functions --------------------------------------------------------*/

/**
 * @brief 设置内存区域为指定值（类似memset）
 * @param dest 目标地址
 * @param value 设置的值
 * @param count 字节数
 * @return 目标地址
 */
static void *mem_set(void *dest, uint8_t value, hal_size_t count)
{
    uint8_t *ptr = (uint8_t *)dest;
    hal_size_t i;

    for (i = 0; i < count; i++)
    {
        ptr[i] = value;
    }

    return dest;
}

/**
 * @brief 复制内存区域（类似memcpy）
 * @param dest 目标地址
 * @param src 源地址
 * @param count 字节数
 * @return 目标地址
 * @warning src和dest不能重叠
 */
static void *mem_copy(void *dest, const void *src, hal_size_t count)
{
    uint8_t *d = (uint8_t*)dest;
    const uint8_t *s = (const uint8_t*)src;
    hal_size_t i;

    for (i = 0; i < count; i++)
    {
        d[i] = s[i];
    }

    return dest;
}

/**
 * @brief 合并相邻的空闲块
 * @param blk 内存块指针
 */
static void merge_adjacent_blocks(mem_blk_t *blk)
{
    uint8_t *blk_start;
    uint8_t *prev_end;
    uint8_t *blk_end;
    uint8_t *next_start;

    blk_start = (uint8_t*)blk;

    /* 向前合并：如果 prev 物理相邻且空闲，prev 吞掉 blk */
    if (blk->prev != (mem_blk_t*)NULL)
    {
        prev_end = (uint8_t*)blk->prev + blk->prev->size;
        if (prev_end == blk_start && blk->prev->is_free)
        {
            blk->prev->size += blk->size;
            blk->prev->next = blk->next;
            if (blk->next != (mem_blk_t*)NULL)
            {
                blk->next->prev = blk->prev;
            }
            blk->is_free = 0;
            blk = blk->prev;
        }
    }

    /* 向后合并：blk 吞掉物理相邻且空闲的 next */
    blk_end = (uint8_t*)blk + blk->size;
    while (blk->next != (mem_blk_t*)NULL)
    {
        next_start = (uint8_t*)blk->next;
        if (blk_end == next_start && blk->next->is_free)
        {
            /* blk 吞掉 next */
            blk->size += blk->next->size;
            blk_end = (uint8_t*)blk + blk->size;
            if (blk->next->next != (mem_blk_t*)NULL)
            {
                blk->next->next->prev = blk;
            }
            blk->next->is_free = 0;
            blk->next = blk->next->next;
        }
        else
        {
            break;
        }
    }
}

/**
 * @brief 将块插入到空闲链表（按地址排序）并合并相邻空闲块
 * @param blk 内存块指针
 */
static void insert_to_free_list(mem_blk_t *blk)
{
    mem_blk_t *curr;
    mem_blk_t *prev;

    blk->is_free = 1;

    /* 空链表，直接插入 */
    if (g_free_list == (mem_blk_t*)NULL)
    {
        g_free_list = blk;
        blk->next = (mem_blk_t*)NULL;
        blk->prev = (mem_blk_t*)NULL;
        return;
    }

    /* 按地址排序插入 */
    curr = g_free_list;
    prev = (mem_blk_t*)NULL;

    while (curr != (mem_blk_t*)NULL && (uint8_t*)curr < (uint8_t*)blk)
    {
        prev = curr;
        curr = curr->next;
    }

    if (prev == (mem_blk_t*)NULL)
    {
        /* 插入到链表头 */
        blk->next = g_free_list;
        blk->prev = (mem_blk_t*)NULL;
        g_free_list->prev = blk;
        g_free_list = blk;
    }
    else
    {
        /* 插入到中间或尾部 */
        blk->next = curr;
        blk->prev = prev;
        prev->next = blk;
        if (curr != (mem_blk_t*)NULL)
        {
            curr->prev = blk;
        }
    }

    /* 合并相邻空闲块 */
    merge_adjacent_blocks(blk);
}

/**
 * @brief 从空闲链表中移除块
 * @param blk 内存块指针
 */
static void remove_from_free_list(mem_blk_t *blk)
{
    if (blk->prev != (mem_blk_t*)NULL)
    {
        blk->prev->next = blk->next;
    }
    else
    {
        g_free_list = blk->next;
    }

    if (blk->next != (mem_blk_t*)NULL)
    {
        blk->next->prev = blk->prev;
    }

    blk->next = (mem_blk_t*)NULL;
    blk->prev = (mem_blk_t*)NULL;
}

/**
 * @brief 在空闲链表中查找合适的块（首次适应算法）
 * @param size 需要的大小（包括头部）
 * @return 找到的块，失败返回NULL
 */
static mem_blk_t *find_free_block(hal_size_t size)
{
    mem_blk_t *blk = g_free_list;

    /* 首次适应：找到第一个足够大的空闲块即返回 */
    while (blk != (mem_blk_t*)NULL)
    {
        if (blk->is_free && blk->size >= size)
        {
            return blk;
        }
        blk = blk->next;
    }

    return (mem_blk_t*)NULL;
}

/**
 * @brief 分割内存块
 * @param blk 内存块指针
 * @param size 需要的大小（已对齐，包含头部）
 * @return 分割后的分配块指针
 */
static mem_blk_t *split_block(mem_blk_t *blk, hal_size_t size)
{
    mem_blk_t *new_blk;
    hal_size_t remaining_size;
    uint8_t *split_addr;

    /* 检查剩余空间是否足够分割 */
    if (blk->size - size < MEM_MIN_SPLIT_SIZE + MEM_BLOCK_HDR_SIZE)
    {
        /* 不分割，直接分配整个块 */
        remove_from_free_list(blk);
        blk->is_free = 0;
        return blk;
    }

    /* 计算分割地址（必须在原块范围内） */
    remaining_size = blk->size - size;
    split_addr = (uint8_t*)blk + size;

    /* 检查分割地址是否正确对齐 */
    if (!SYS_IS_ADDR_ALIGNED(split_addr))
    {
        /* 未对齐，不分割以避免内存碎片 */
        remove_from_free_list(blk);
        blk->is_free = 0;
        return blk;
    }

    /* 更新原块大小 */
    blk->size = size;

    /* 创建新的空闲块 */
    new_blk = (mem_blk_t*)split_addr;
    new_blk->size = remaining_size;
    new_blk->is_free = 1;
    new_blk->next = (mem_blk_t*)NULL;
    new_blk->prev = (mem_blk_t*)NULL;

    /* 从空闲链表移除分配的块 */
    remove_from_free_list(blk);
    blk->is_free = 0;

    /* 将分割出的空闲块插入空闲链表 */
    insert_to_free_list(new_blk);

    return blk;
}
