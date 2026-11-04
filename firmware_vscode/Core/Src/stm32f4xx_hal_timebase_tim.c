/* HAL uses TIM2 at 1 kHz; SysTick belongs exclusively to FreeRTOS.
 * TIM2 is configured here without enabling the whole HAL TIM module. */
#include "main.h"

HAL_StatusTypeDef HAL_InitTick(uint32_t priority)
{
    if (priority >= (1UL << __NVIC_PRIO_BITS)) return HAL_ERROR;
    __HAL_RCC_TIM2_CLK_ENABLE();
    HAL_NVIC_DisableIRQ(TIM2_IRQn);
    RCC_ClkInitTypeDef clocks;
    uint32_t latency;
    HAL_RCC_GetClockConfig(&clocks, &latency);
    uint32_t timer_clock = HAL_RCC_GetPCLK1Freq();
    if (clocks.APB1CLKDivider != RCC_HCLK_DIV1) timer_clock *= 2U;
    if (timer_clock < 1000000U || timer_clock % 1000000U != 0) return HAL_ERROR;
    TIM2->CR1 = 0;
    TIM2->PSC = timer_clock / 1000000U - 1U;
    TIM2->ARR = 999U;
    TIM2->CNT = 0;
    TIM2->EGR = TIM_EGR_UG;
    TIM2->SR = 0;
    HAL_NVIC_SetPriority(TIM2_IRQn, priority, 0);
    HAL_NVIC_ClearPendingIRQ(TIM2_IRQn);
    uwTickPrio = priority;
    TIM2->DIER = TIM_DIER_UIE;
    HAL_NVIC_EnableIRQ(TIM2_IRQn);
    TIM2->CR1 = TIM_CR1_CEN;
    return HAL_OK;
}

void TIM2_IRQHandler(void)
{
    if ((TIM2->SR & TIM_SR_UIF) && (TIM2->DIER & TIM_DIER_UIE)) {
        TIM2->SR = ~TIM_SR_UIF;
        HAL_IncTick();
    }
}

void HAL_SuspendTick(void) { TIM2->DIER &= ~TIM_DIER_UIE; }
void HAL_ResumeTick(void) { TIM2->DIER |= TIM_DIER_UIE; }
