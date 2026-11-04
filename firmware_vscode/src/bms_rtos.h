#ifndef BMS_RTOS_H
#define BMS_RTOS_H

#include "bq76922_driver.h"
#include <stdbool.h>
#include <stdint.h>

#define BMS_POLL_INTERVAL_MS 250U
#define BMS_UI_INTERVAL_MS   50U
#define BMS_STALE_AFTER_MS   1000U
#define BMS_MAX_CAPACITY_MAH 4000.0f
#define BMS_COMMAND_DEPTH   4U
#define BMS_BATTERY_STACK_WORDS 768U
#define BMS_UI_STACK_WORDS      1024U

typedef enum {
    BMS_FET_ENABLE,
    BMS_ALL_FETS_OFF,
    BMS_SHUTDOWN,
    BMS_RESET_PASSQ,
    BMS_PF_RESET,
    BMS_OCD_RECOVER,
    BMS_SCD_RECOVER,
    BMS_ACTION_COUNT
} BmsAction;

typedef struct {
    BQ_Telemetry_t telemetry;
    BQ_Faults_t faults;
    float soc_percent;
    float remaining_mah;
    uint32_t sampled_at_ms;  /* Only a complete successful poll advances this. */
    uint32_t published_at_ms;
    uint32_t read_failures;
    bool valid;
    bool balancing_valid;
    bool charging;
    BQ_Status_t status;
} BmsSnapshot;

typedef struct {
    uint32_t id;
    BmsAction action;
    BQ_Status_t status;
} BmsCommandResult;

/* Debugger diagnostics; each task writes only its own fields. Stack units
 * are 32-bit words, not bytes. No task uses these fields for synchronization. */
typedef struct {
    volatile uint32_t battery_stack_free_words;
    volatile uint32_t ui_stack_free_words;
    volatile uint32_t battery_deadline_misses;
    volatile uint32_t ui_deadline_misses;
    volatile uint32_t max_battery_cycle_ms;
    volatile uint32_t command_queue_full;
    volatile uint32_t completion_backpressure;
    volatile uint32_t display_failures;
    volatile uint32_t battery_cycles;
    volatile uint32_t startup_command_failed;
} BmsRtosDiagnostics;

extern BmsRtosDiagnostics BmsRtosStats;

/* Called after GPIO/I2C/SPI initialization; returns only on fatal failure. */
void BmsRtosStart(void);
/* UI-only queue accessors; all operations are nonblocking. */
bool BmsTakeSnapshot(BmsSnapshot *snapshot);
bool BmsSubmitCommand(BmsAction action, uint32_t id);
bool BmsTakeCommandResult(BmsCommandResult *result);
bool BmsSnapshotUsable(const BmsSnapshot *snapshot, uint32_t now_ms);
/* Implemented by main.c; called only by the UI task. */
void BmsUiTask(void *argument);

#endif
