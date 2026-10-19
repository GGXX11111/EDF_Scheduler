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

/* Exported types ------------------------------------------------------------*/

/******************************************************************************/
/*                        数据类型定义                                      */
/******************************************************************************/

/* hal_size_t数据类型 */
typedef uint32_t                hal_size_t;

/* Exported variables --------------------------------------------------------*/

/* Exported defines ----------------------------------------------------------*/

/**
 * @brief 通用执行结果状态码
 */
typedef enum edf_status
{
    EDF_ERR  = 0,       // 失败
    EDF_OK   = 1        // 成功
} edf_status_t;

/**
 * @brief 布尔类型
 */
typedef enum edf_bool
{
    EDF_FALSE = 0,      // 假
    EDF_TRUE  = 1       // 真
} edf_bool_t;

/******************************************************************************/
/*                        中断控制与临界区保护                                */
/******************************************************************************/

/* ==================== 当前中断开关实现 ==================== */

/* 中断开关(用户实现) */

/* ========================================================= */

/* 中断开关（通用空实现） */
#ifndef HAL_DISABLE_INTERRUPTS
    #define HAL_DISABLE_INTERRUPTS()            /* empty */
#endif
#ifndef HAL_ENABLE_INTERRUPTS
    #define HAL_ENABLE_INTERRUPTS()             /* empty */
#endif

/* 临界区嵌套计数器 */
extern volatile hal_size_t g_critical_count;

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

/* Exported functions --------------------------------------------------------*/

/******************************************************************************/
/*                        硬件定时器接口                                      */
/******************************************************************************/

/**
 * @brief 硬件定时器初始化
 * @param tick_freq 系统时钟基准频率（Hz），如1000表示1ms时钟基准
 * @return 成功返回EDF_OK，失败返回EDF_ERR
 */
edf_status_t hal_timer_init(uint32_t tick_freq);

/**
 * @brief 硬件定时器启动
 * @return 成功返回EDF_OK，失败返回EDF_ERR
 */
edf_status_t hal_timer_start(void);

/**
 * @brief 硬件定时器停止
 * @return 成功返回EDF_OK，失败返回EDF_ERR
 */
edf_status_t hal_timer_stop(void);

/**
 * @brief 硬件定时器中断服务函数
 * @note 由 硬件定时器到期 Handler 调用，在中断上下文中执行
 */
void hal_timer_isr(void);

#ifdef __cplusplus
}
#endif

#endif /* EDF_BASE_H */
