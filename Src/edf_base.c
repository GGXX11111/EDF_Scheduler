/**
 ******************************************************************************
 * @file    edf_base.c
 * @brief   框架基础实现：临界区计数器与硬件定时器移植层
 ******************************************************************************
 */

/* Includes ------------------------------------------------------------------*/

#include "edf_base.h"
#include "cubemx.h"

/* Private constants ---------------------------------------------------------*/

/* Private types -------------------------------------------------------------*/

/* Private variables ---------------------------------------------------------*/

/* Private defines -----------------------------------------------------------*/

/* Private function prototypes ----------------------------------------------*/

/* Exported constants --------------------------------------------------------*/

/* Exported variables --------------------------------------------------------*/

/* 临界区嵌套计数器 */
volatile hal_size_t g_critical_count = 0;

/* Exported functions --------------------------------------------------------*/

/* 软件定时器更新函数（由timer_mgr模块提供） */
extern void timer_update(void);

/**
 * @brief 硬件定时器初始化
 * @param tick_freq 系统时钟基准频率（Hz），如1000表示1ms时钟基准
 * @return 成功返回EDF_OK，失败返回EDF_ERR
 * @note 更换 MCU 时需重写此函数
 */
edf_status_t hal_timer_init(uint32_t tick_freq)
{
    uint32_t load_value;

    if (tick_freq == 0U)
    {
        return EDF_ERR;
    }

    /* 计算重装载值 */
    load_value = SystemCoreClock / tick_freq;
    if (load_value == 0U || load_value > 0x00FFFFFFUL)
    {
        return EDF_ERR;  /* 频率不合法或超出 SysTick 24 位计数范围 */
    }

    /* 配置 SysTick NVIC 优先级 */
    HAL_NVIC_SetPriority(SysTick_IRQn, 3, 0);

    /* 配置 SysTick */
    SysTick->LOAD = load_value - 1U;              /* 设置重装载值 */
    SysTick->VAL  = 0U;                           /* 清空当前计数器 */
    SysTick->CTRL = SysTick_CTRL_CLKSOURCE_Msk    /* 使用处理器时钟 */
                  | SysTick_CTRL_TICKINT_Msk;     /* 使能中断（暂不启动计数） */

    return EDF_OK;
}

/**
 * @brief 硬件定时器启动
 * @return 成功返回EDF_OK，失败返回EDF_ERR
 * @note 更换 MCU 时需重写此函数
 */
edf_status_t hal_timer_start(void)
{
    /* 使能 SysTick 计数器 */
    SysTick->CTRL |= SysTick_CTRL_ENABLE_Msk;

    return EDF_OK;
}

/**
 * @brief 硬件定时器停止
 * @return 成功返回EDF_OK，失败返回EDF_ERR
 * @note 更换 MCU 时需重写此函数
 */
edf_status_t hal_timer_stop(void)
{
    /* 禁用 SysTick 计数器 */
    SysTick->CTRL &= ~SysTick_CTRL_ENABLE_Msk;

    return EDF_OK;
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

/**
 * @brief SysTick 中断处理函数
 * @note 与中断向量表中的 SysTick_Handler 对应
 */
void SysTick_Handler(void)
{
    hal_timer_isr();
}
