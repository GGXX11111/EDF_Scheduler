/**
 ******************************************************************************
 * @file    edf_base.h
 * @brief   框架基础定义：配置项、公共类型与移植层接口
 ******************************************************************************
 */

#ifndef EDF_BASE_H
#define EDF_BASE_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* Exported constants --------------------------------------------------------*/

#ifndef EDF_TICK_FREQ_HZ
	#define EDF_TICK_FREQ_HZ        1000		// 系统时钟节拍频率（Hz），典型值1000（1kHz，每tick 1ms）
#endif

#ifndef MEM_POOL_SIZE
	#define MEM_POOL_SIZE           4096		// 内存池总大小（字节），建议4KB/8KB等2的幂次
#endif

#ifndef MEM_STATS_ENABLE
	#define MEM_STATS_ENABLE        0			// 是否启用内存统计：0-禁用（节省资源），1-启用（增加少量开销）
#endif

/* Exported types ------------------------------------------------------------*/

/******************************************************************************/
/*                        数据类型定义                                      */
/******************************************************************************/

typedef union
{
	struct 
	{
		uint8_t bit0:1;
		uint8_t bit1:1;
		uint8_t bit2:1;
		uint8_t bit3:1;
		uint8_t bit4:1;
		uint8_t bit5:1;
		uint8_t bit6:1;
		uint8_t bit7:1;
	} bits;
	uint8_t ubyte; 
	int8_t sbyte;
	char cbyte;
} byte_blk_t;

typedef union
{
	struct 
	{
		uint16_t bit0:1;
		uint16_t bit1:1;
		uint16_t bit2:1;
		uint16_t bit3:1;
		uint16_t bit4:1;
		uint16_t bit5:1;
		uint16_t bit6:1;
		uint16_t bit7:1;
		uint16_t bit8:1;
		uint16_t bit9:1;
		uint16_t bit10:1;
		uint16_t bit11:1;
		uint16_t bit12:1;
		uint16_t bit13:1;
		uint16_t bit14:1;
		uint16_t bit15:1;
	} bits;
	uint16_t uhalfword;
	int16_t shalfword;
	uint8_t bytes[2];
	int8_t sbytes[2];
	char cbytes[2];
} halfword_blk_t;

typedef union
{
	struct 
	{
		uint32_t bit0:1;
		uint32_t bit1:1;
		uint32_t bit2:1;
		uint32_t bit3:1;
		uint32_t bit4:1;
		uint32_t bit5:1;
		uint32_t bit6:1;
		uint32_t bit7:1;
		uint32_t bit8:1;
		uint32_t bit9:1;
		uint32_t bit10:1;
		uint32_t bit11:1;
		uint32_t bit12:1;
		uint32_t bit13:1;
		uint32_t bit14:1;
		uint32_t bit15:1;
		uint32_t bit16:1;
		uint32_t bit17:1;
		uint32_t bit18:1;
		uint32_t bit19:1;
		uint32_t bit20:1;
		uint32_t bit21:1;
		uint32_t bit22:1;
		uint32_t bit23:1;
		uint32_t bit24:1;
		uint32_t bit25:1;
		uint32_t bit26:1;
		uint32_t bit27:1;
		uint32_t bit28:1;
		uint32_t bit29:1;
		uint32_t bit30:1;
		uint32_t bit31:1;		
	} bits;	
	uint32_t uword;	
	int32_t sword;
	float fword;
	uint16_t uhalfwords[2];	
	int16_t shalfwords[2];	
	uint8_t ubytes[4];	
	int8_t sbytes[4];
	char cbytes[4];
	void *ptr;
	char *strings;
} word_blk_t;

/* 硬件数据对齐类型（32位系统使用uint32_t，64位系统应改为uint64_t） */
typedef uint32_t                hal_data_align_t;

/* hal_size_t数据类型（基于硬件对齐类型，确保与内存对齐要求一致） */
typedef hal_data_align_t        hal_size_t;

/* 任务相关类型 */
typedef uint32_t                task_event_t;       // 任务事件类型
typedef uint8_t                 task_status_t;      // 任务状态
typedef uint8_t                 task_priority_t;    // 任务优先级

/* 定时器相关类型 */
typedef uint32_t                timer_tick_t;       // 时钟节拍类型
typedef uint8_t                 timer_mode_t;       // 定时器模式
typedef uint8_t                 timer_status_t;     // 定时器状态

/* 消息相关类型 */
typedef uint16_t                msg_count_t;        // 消息数量类型
typedef uint16_t                msg_len_t;          // 消息长度类型

/* Exported variables --------------------------------------------------------*/

/* Exported defines ----------------------------------------------------------*/

/* 通用状态码 */
#define EDF_SUCCESS             0x01    // 成功（真）
#define EDF_FAILURE             0x00    // 失败（假）

/* 任务事件标志 */
#define EVENT_NULL              0x00

/* 任务状态 */
#define TASK_STATUS_READY       0x00    // 就绪
#define TASK_STATUS_RUNNING     0x01    // 运行中
#define TASK_STATUS_BLOCKED     0x02    // 阻塞
#define TASK_STATUS_SUSPENDED   0x03    // 挂起

/* 任务优先级（数值越小优先级越高） */
#define TASK_PRIORITY_HIGHEST   0       // 最高优先级
#define TASK_PRIORITY_HIGH      1       // 高优先级
#define TASK_PRIORITY_ABOVE_NOR 2       // 中高优先级
#define TASK_PRIORITY_NORMAL    3       // 正常优先级
#define TASK_PRIORITY_BELOW_NOR 4       // 中低优先级
#define TASK_PRIORITY_LOW       5       // 低优先级
#define TASK_PRIORITY_VERY_LOW  6       // 很低优先级
#define TASK_PRIORITY_LOWEST    7       // 最低优先级
#define TASK_PRIORITY_COUNT     8       // 优先级数量

/* 硬件定时器状态 */
#define TIMER_HW_INACTIVE       0x00    // 未激活
#define TIMER_HW_ACTIVE         0x01    // 已激活

/* 软件定时器模式 */
#define TIMER_MODE_SINGLE       0x00    // 单次定时器
#define TIMER_MODE_PERIODIC     0x01    // 周期定时器

/* 软件定时器状态 */
#define TIMER_STATUS_IDLE       0x00    // 空闲
#define TIMER_STATUS_RUNNING    0x01    // 运行中
#define TIMER_STATUS_STOPPED    0x02    // 已停止
#define TIMER_STATUS_EXPIRED    0x03    // 已超时

/******************************************************************************/
/*                        内存对齐相关宏定义                                  */
/******************************************************************************/

/* 系统内存对齐大小 */
#ifndef SYS_MEM_ALIGN_SIZE
	#define SYS_MEM_ALIGN_SIZE              sizeof(hal_data_align_t)
#endif

/**
 * @brief 向上对齐宏
 * @param size  需要对齐的大小
 * @param align 对齐字节数
 * @return 对齐后的大小（不小于原始值）
 * @note align 必须为2的幂，非2的幂会导致结果错误
 */
#ifndef ALIGN_UP
	#define ALIGN_UP(size, align)           (((size) + ((align) - 1)) & ~((align) - 1))
#endif

/**
 * @brief 向下对齐宏
 * @param size  需要对齐的大小
 * @param align 对齐字节数
 * @return 对齐后的大小（不大于原始值）
 * @note align 必须为2的幂，非2的幂会导致结果错误
 */
#ifndef ALIGN_DOWN
	#define ALIGN_DOWN(size, align)         ((size) & ~((align) - 1))
#endif

/**
 * @brief 系统默认向上对齐
 * @param size 需要对齐的大小
 * @return 按系统对齐大小向上对齐后的值
 */
#ifndef SYS_ALIGN_UP
	#define SYS_ALIGN_UP(size)              ALIGN_UP(size, SYS_MEM_ALIGN_SIZE)
#endif

/**
 * @brief 系统默认向下对齐
 * @param size 需要对齐的大小
 * @return 按系统对齐大小向下对齐后的值
 */
#ifndef SYS_ALIGN_DOWN
	#define SYS_ALIGN_DOWN(size)            ALIGN_DOWN(size, SYS_MEM_ALIGN_SIZE)
#endif

/**
 * @brief 检查大小是否对齐
 * @param size 需要检查的大小
 * @return 已对齐返回非零，否则返回零
 */
#ifndef SYS_IS_ALIGNED
	#define SYS_IS_ALIGNED(size)            (((size) & (SYS_MEM_ALIGN_SIZE - 1)) == 0)
#endif

/**
 * @brief 检查地址是否对齐
 * @param addr 需要检查的地址
 * @return 已对齐返回非零，否则返回零
 */
#ifndef SYS_IS_ADDR_ALIGNED
	#define SYS_IS_ADDR_ALIGNED(addr)       (((hal_data_align_t)(addr) & (SYS_MEM_ALIGN_SIZE - 1)) == 0)
#endif

/******************************************************************************/
/*                        结构体操作相关宏定义                                */
/******************************************************************************/

/**
 * @brief 求结构体成员在结构体中的字节偏移
 * @param type   结构体类型
 * @param member 成员名
 * @return 成员相对结构体起始的偏移量（字节）
 */
#ifndef OFFSET_OF
	#define OFFSET_OF(type, member)         ((uint32_t)&((type *)0)->member)
#endif

/**
 * @brief 由结构体成员指针反推结构体起始地址
 * @param ptr    结构体成员指针
 * @param type   结构体类型
 * @param member 成员名
 * @return 结构体起始地址
 */
#ifndef CONTAINER_OF
	#define CONTAINER_OF(ptr, type, member) ((type *)((char *)(ptr) - OFFSET_OF(type, member)))
#endif

/******************************************************************************/
/*                        中断控制与临界区保护                                */
/******************************************************************************/

/* ==================== 当前中断开关实现 ==================== */

/* 中断开关(用户实现) */
#include "stm32g0xx.h"
#define HAL_DISABLE_INTERRUPTS()                __disable_irq()
#define HAL_ENABLE_INTERRUPTS()                 __enable_irq()

/* ========================================================= */

/* 中断开关（通用空实现） */
#ifndef HAL_DISABLE_INTERRUPTS
    #define HAL_DISABLE_INTERRUPTS()            /* empty */
#endif
#ifndef HAL_ENABLE_INTERRUPTS
    #define HAL_ENABLE_INTERRUPTS()             /* empty */
#endif

/* 临界区嵌套计数器*/
extern volatile hal_data_align_t g_critical_count;

/* 进入临界区 */
#ifndef HAL_ENTER_CRITICAL_SECTION
    #define HAL_ENTER_CRITICAL_SECTION()        do { \
                                                    if (g_critical_count == 0) { \
                                                        HAL_DISABLE_INTERRUPTS(); \
                                                    } \
                                                    g_critical_count++; \
                                                } while (0)
#endif

/* 退出临界区 */
#ifndef HAL_EXIT_CRITICAL_SECTION
    #define HAL_EXIT_CRITICAL_SECTION()         do { \
                                                    if (g_critical_count > 0) { \
                                                        g_critical_count--; \
                                                        if (g_critical_count == 0) { \
                                                            HAL_ENABLE_INTERRUPTS(); \
                                                        } \
                                                    } \
                                                } while (0)
#endif

/******************************************************************************/
/*                        原子操作宏定义                                      */
/******************************************************************************/

/* ==================== 当前原子操作实现 ==================== */

/* 原子操作(用户实现) */

/* ========================================================= */

/* 原子操作（通用实现） */
#ifndef ATOMIC_INC
    #define ATOMIC_INC(bw, var)                 do { HAL_ENTER_CRITICAL_SECTION(); (var)++; HAL_EXIT_CRITICAL_SECTION(); } while (0)
#endif
#ifndef ATOMIC_DEC
    #define ATOMIC_DEC(bw, var)                 do { HAL_ENTER_CRITICAL_SECTION(); (var)--; HAL_EXIT_CRITICAL_SECTION(); } while (0)
#endif
#ifndef ATOMIC_ADD
    #define ATOMIC_ADD(bw, var, n)              do { HAL_ENTER_CRITICAL_SECTION(); (var) += (n); HAL_EXIT_CRITICAL_SECTION(); } while (0)
#endif
#ifndef ATOMIC_SUB
    #define ATOMIC_SUB(bw, var, n)              do { HAL_ENTER_CRITICAL_SECTION(); (var) -= (n); HAL_EXIT_CRITICAL_SECTION(); } while (0)
#endif
#ifndef ATOMIC_BIT_SET
    #define ATOMIC_BIT_SET(bw, var, m)          do { HAL_ENTER_CRITICAL_SECTION(); (var) |= (m); HAL_EXIT_CRITICAL_SECTION(); } while (0)
#endif
#ifndef ATOMIC_BIT_CLEAR
    #define ATOMIC_BIT_CLEAR(bw, var, m)        do { HAL_ENTER_CRITICAL_SECTION(); (var) &= ~(m); HAL_EXIT_CRITICAL_SECTION(); } while (0)
#endif
#ifndef ATOMIC_BIT_TOGGLE
    #define ATOMIC_BIT_TOGGLE(bw, var, m)       do { HAL_ENTER_CRITICAL_SECTION(); (var) ^= (m); HAL_EXIT_CRITICAL_SECTION(); } while (0)
#endif

/******************************************************************************/
/*                        低功耗操作宏定义                                    */
/******************************************************************************/

/* ==================== 当前低功耗操作实现 =================== */

/* 低功耗操作(用户实现) */	
#define HAL_WFI()								__WFI()

/* ========================================================= */

/* 低功耗操作（通用实现） */
#ifndef HAL_WFI
	#define HAL_WFI()							/* empty */
#endif

/* Exported functions --------------------------------------------------------*/

/******************************************************************************/
/*                        硬件定时器接口                                      */
/******************************************************************************/

/**
 * @brief 硬件定时器初始化
 * @param tick_freq 系统时钟基准频率（Hz），如1000表示1ms时钟基准
 * @return 成功返回EDF_SUCCESS，失败返回EDF_FAILURE
 */
uint8_t hal_timer_init(timer_tick_t tick_freq);

/**
 * @brief 硬件定时器启动
 * @return 成功返回EDF_SUCCESS，失败返回EDF_FAILURE
 */
uint8_t hal_timer_start(void);

/**
 * @brief 硬件定时器停止
 * @return 成功返回EDF_SUCCESS，失败返回EDF_FAILURE
 */
uint8_t hal_timer_stop(void);

/**
 * @brief 硬件定时器中断服务函数
 * @note 由 硬件定时器到期 Handler 调用，在中断上下文中执行
 */
void hal_timer_isr(void);

#ifdef __cplusplus
}
#endif

#endif /* EDF_BASE_H */
