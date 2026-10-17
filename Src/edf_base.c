/**
 ******************************************************************************
 * @file    edf_base.c
 * @brief   框架基础实现：临界区计数器与硬件定时器移植层
 ******************************************************************************
 */

/* Includes ------------------------------------------------------------------*/

#include "edf_base.h"

/* Private constants ---------------------------------------------------------*/

/* Private types -------------------------------------------------------------*/

/* Private variables ---------------------------------------------------------*/

/* Private defines -----------------------------------------------------------*/

/* Private function prototypes ----------------------------------------------*/

/* Exported constants --------------------------------------------------------*/

/* Exported variables --------------------------------------------------------*/

/* 临界区嵌套计数器 */
volatile hal_data_align_t g_critical_count = 0;

/* Exported functions --------------------------------------------------------*/

/*****************************************************************************/
/*                        硬件定时器相关函数实现                                */
/*****************************************************************************/

/* 软件定时器更新函数（由timer_mgr模块提供） */
extern void timer_update(void);

/**
 * @brief 硬件定时器初始化
 * @param tick_freq 系统时钟基准频率（Hz），如1000表示1ms时钟基准
 * @return 成功返回EDF_SUCCESS，失败返回EDF_FAILURE
 * @note 更换 MCU 时需重写此函数
 */
uint8_t hal_timer_init(timer_tick_t tick_freq)
{
	(void)tick_freq;
	return EDF_SUCCESS;
}

/**
 * @brief 硬件定时器启动
 * @return EDF_SUCCESS
 * @note 更换 MCU 时需重写此函数
 */
uint8_t hal_timer_start(void)
{
	return EDF_SUCCESS;
}

/**
 * @brief 硬件定时器停止
 * @return EDF_SUCCESS
 * @note 更换 MCU 时需重写此函数
 */
uint8_t hal_timer_stop(void)
{
	return EDF_SUCCESS;
}

/**
 * @brief 硬件定时器中断服务函数
 * @note 由 硬件定时器到期 Handler 调用，在中断上下文中执行
 */
void hal_timer_isr(void)
{
    timer_update();
}

/* Private functions --------------------------------------------------------*/
