#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H

#include <stdint.h>
extern uint32_t SystemCoreClock;
void BmsRtosAssert(const char *file, unsigned line);

#define configUSE_PREEMPTION                     1
#define configUSE_TIME_SLICING                    1
#define configUSE_PORT_OPTIMISED_TASK_SELECTION   1
#define configCPU_CLOCK_HZ                       SystemCoreClock
#define configTICK_RATE_HZ                       ((TickType_t)1000)
#define configMAX_PRIORITIES                     4
#define configMINIMAL_STACK_SIZE                 128U
#define configMAX_TASK_NAME_LEN                  12
#define configUSE_16_BIT_TICKS                   0
#define configIDLE_SHOULD_YIELD                   1
#define configUSE_TICKLESS_IDLE                  0
#define configSUPPORT_STATIC_ALLOCATION          1
#define configSUPPORT_DYNAMIC_ALLOCATION         0
#define configUSE_TIMERS                         0
#define configUSE_MUTEXES                        0
#define configUSE_RECURSIVE_MUTEXES              0
#define configUSE_COUNTING_SEMAPHORES            0
#define configQUEUE_REGISTRY_SIZE                3
#define configUSE_TASK_NOTIFICATIONS            0
#define configUSE_IDLE_HOOK                      0
#define configUSE_TICK_HOOK                      0
#define configCHECK_FOR_STACK_OVERFLOW           2
#define configUSE_MALLOC_FAILED_HOOK             1
#define configUSE_NEWLIB_REENTRANT               0
#define configUSE_TRACE_FACILITY                 0
#define configUSE_STATS_FORMATTING_FUNCTIONS     0
#define configGENERATE_RUN_TIME_STATS            0

/* STM32F411 implements four NVIC priority bits (0 = highest urgency).
 * Only priorities 5..15 may call FromISR APIs. TIM2 uses 4 and calls none. */
#define configPRIO_BITS                          4
#define configLIBRARY_LOWEST_INTERRUPT_PRIORITY  15
#define configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY 5
#define configKERNEL_INTERRUPT_PRIORITY          (15U << (8U - configPRIO_BITS))
#define configMAX_SYSCALL_INTERRUPT_PRIORITY      (5U << (8U - configPRIO_BITS))

#define INCLUDE_vTaskDelay                       1
#define INCLUDE_vTaskDelayUntil                  1
#define INCLUDE_xTaskGetSchedulerState           1
#define INCLUDE_uxTaskGetStackHighWaterMark       1
#define INCLUDE_vTaskDelete                      0
#define INCLUDE_vTaskSuspend                     0

#define configASSERT(condition) do { if (!(condition)) BmsRtosAssert(__FILE__, __LINE__); } while (0)

// Port defines the vector handlers directly, do not add C wrappers.
#define vPortSVCHandler SVC_Handler
#define xPortPendSVHandler PendSV_Handler
#define xPortSysTickHandler SysTick_Handler

#endif
