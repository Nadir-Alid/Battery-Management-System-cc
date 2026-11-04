#include "bms_rtos.h"
#include "coulomb_counter.h"
#include "main.h"
#include "FreeRTOS.h"
#include "queue.h"
#include "task.h"

extern I2C_HandleTypeDef hi2c3;

typedef struct {
    uint32_t id;
    BmsAction action;
} BmsCommand;

static QueueHandle_t snapshot_queue, command_queue, result_queue;
static StaticQueue_t snapshot_control, command_control, result_control;
static uint8_t snapshot_storage[sizeof(BmsSnapshot)];
static uint8_t command_storage[BMS_COMMAND_DEPTH * sizeof(BmsCommand)];
static uint8_t result_storage[BMS_COMMAND_DEPTH * sizeof(BmsCommandResult)];
static StaticTask_t battery_control, ui_control, idle_control;
static StackType_t battery_stack[BMS_BATTERY_STACK_WORDS];
static StackType_t ui_stack[BMS_UI_STACK_WORDS];
static StackType_t idle_stack[configMINIMAL_STACK_SIZE];

BmsRtosDiagnostics BmsRtosStats;

/* These objects belong exclusively to BatteryTask after the scheduler starts. */
static BQ_Handle_t battery;
static CoulombCounter_t counter;
static BmsSnapshot latest;
static bool charging;
static uint32_t charge_start_ms, last_balance_ms;

bool BmsSnapshotUsable(const BmsSnapshot *snapshot, uint32_t now_ms)
{
    return snapshot != NULL && snapshot->valid &&
        (uint32_t)(now_ms - snapshot->sampled_at_ms) < BMS_STALE_AFTER_MS;
}

bool BmsTakeSnapshot(BmsSnapshot *snapshot)
{
    return snapshot != NULL && xQueueReceive(snapshot_queue, snapshot, 0) == pdPASS;
}

bool BmsSubmitCommand(BmsAction action, uint32_t id)
{
    if ((unsigned)action >= BMS_ACTION_COUNT) return false;
    BmsCommand request = {.id = id, .action = action};
    if (xQueueSend(command_queue, &request, 0) != pdPASS) {
        ++BmsRtosStats.command_queue_full;
        return false;
    }
    return true;
}

bool BmsTakeCommandResult(BmsCommandResult *result)
{
    return result != NULL && xQueueReceive(result_queue, result, 0) == pdPASS;
}

static void Publish(void)
{
    latest.published_at_ms = HAL_GetTick();
    configASSERT(xQueueOverwrite(snapshot_queue, &latest) == pdPASS);
}

static BQ_Status_t ReadBattery(void)
{
    BQ_Status_t status = BQ_Poll(&battery);
    if (status == BQ_OK) status = BQ_ReadFETStatus(&battery);
    if (status == BQ_OK) status = CC_Update(&counter, &battery);
    uint32_t now = HAL_GetTick();
    if (status == BQ_OK) {
        if (battery.telem.pack_current_ma > 50) {
            if (!charging) {
                charging = true;
                charge_start_ms = now;
            }
            if ((uint32_t)(now - charge_start_ms) >= 5000U &&
                (uint32_t)(now - last_balance_ms) >= 2000U) {
                last_balance_ms = now;
                status = BQ_ReadBalancing(&battery);
                latest.balancing_valid = status == BQ_OK;
            }
        } else {
            charging = false;
            battery.telem.cb_active_mask = 0;
            latest.balancing_valid = true;
        }
    }
    if (status == BQ_OK) {
        latest.telemetry = battery.telem;
        latest.faults = battery.faults;
        latest.soc_percent = counter.soc_percent;
        latest.remaining_mah = counter.effective_passq_mah;
        latest.sampled_at_ms = HAL_GetTick();
        latest.valid = true;
        latest.charging = charging;
    } else {
        /* Retain the last complete values, but never label them as fresh. */
        ++latest.read_failures;
        latest.valid = false;
        latest.balancing_valid = false;
        latest.charging = false;
        charging = false;
        counter.full_candidate = false;
    }
    latest.status = status;
    return status;
}

static BQ_Status_t ExecuteCommand(BmsAction action)
{
    switch (action) {
        case BMS_FET_ENABLE:   return BQ_FETEnable(&battery);
        case BMS_ALL_FETS_OFF: return BQ_AllFETsOff(&battery);
        case BMS_SHUTDOWN:     return BQ_Shutdown(&battery);
        case BMS_RESET_PASSQ:  return CC_Reset(&counter, &battery);
        case BMS_PF_RESET:     return BQ_PFReset(&battery);
        case BMS_OCD_RECOVER:  return BQ_OCDLRecover(&battery);
        case BMS_SCD_RECOVER:  return BQ_SCDLRecover(&battery);
        default:              return BQ_ERROR;
    }
}

static void BatteryTask(void *argument)
{
    (void)argument;
    configASSERT(BQ_Init(&battery, &hi2c3) == BQ_OK);
    CC_Init(&counter, BMS_MAX_CAPACITY_MAH);
    /* Same startup request as the original firmware; no protection settings. */
    if (BQ_FETEnable(&battery) != BQ_OK) BmsRtosStats.startup_command_failed = 1;
    TickType_t release = xTaskGetTickCount();
    const TickType_t period = pdMS_TO_TICKS(BMS_POLL_INTERVAL_MS);
    BmsCommandResult reply = {0};
    bool reply_pending = false;

    for (;;) {
        uint32_t start_ms = HAL_GetTick();
        ReadBattery();
        ++BmsRtosStats.battery_cycles;

        /* Retain a completion until delivered. Full result queues must never
         * block measurements, lose an ACK, or execute the same command twice. */
        if (reply_pending) {
            reply_pending = xQueueSend(result_queue, &reply, 0) != pdPASS;
            if (reply_pending) ++BmsRtosStats.completion_backpressure;
        }
        BmsCommand request;
        /* At most one command per measurement period bounds command traffic. */
        if (!reply_pending && xQueueReceive(command_queue, &request, 0) == pdPASS) {
            reply.id = request.id;
            reply.action = request.action;
            reply.status = ExecuteCommand(request.action);
            BQ_Status_t fet_status = BQ_ReadFETStatus(&battery);
            if (reply.status == BQ_OK) reply.status = fet_status;
            if (fet_status == BQ_OK) latest.faults = battery.faults;
            if (reply.status != BQ_OK) {
                latest.valid = false;
                latest.charging = false;
                latest.status = reply.status;
                charging = false;
                counter.full_candidate = false;
            }
            reply_pending = xQueueSend(result_queue, &reply, 0) != pdPASS;
            if (reply_pending) ++BmsRtosStats.completion_backpressure;
        }
        Publish();
        BmsRtosStats.battery_stack_free_words = uxTaskGetStackHighWaterMark(NULL);
        uint32_t cycle_ms = HAL_GetTick() - start_ms;
        if (cycle_ms > BmsRtosStats.max_battery_cycle_ms)
            BmsRtosStats.max_battery_cycle_ms = cycle_ms;
        /* Re-anchor after an overrun instead of spinning through catch-up polls. */
        if ((TickType_t)(xTaskGetTickCount() - release) >= period) {
            ++BmsRtosStats.battery_deadline_misses;
            release = xTaskGetTickCount();
        }
        vTaskDelayUntil(&release, period);
    }
}

void BmsRtosStart(void)
{
    snapshot_queue = xQueueCreateStatic(1, sizeof(BmsSnapshot), snapshot_storage, &snapshot_control);
    command_queue = xQueueCreateStatic(BMS_COMMAND_DEPTH, sizeof(BmsCommand), command_storage, &command_control);
    result_queue = xQueueCreateStatic(BMS_COMMAND_DEPTH, sizeof(BmsCommandResult), result_storage, &result_control);
    configASSERT(snapshot_queue && command_queue && result_queue);
    vQueueAddToRegistry(snapshot_queue, "telemetry");
    vQueueAddToRegistry(command_queue, "commands");
    vQueueAddToRegistry(result_queue, "results");
    latest.status = BQ_ERROR;
    Publish();
    configASSERT(xTaskCreateStatic(BatteryTask, "battery", BMS_BATTERY_STACK_WORDS,
                 NULL, 2, battery_stack, &battery_control) != NULL);
    configASSERT(xTaskCreateStatic(BmsUiTask, "ui", BMS_UI_STACK_WORDS,
                 NULL, 1, ui_stack, &ui_control) != NULL);
    vTaskStartScheduler();
    BmsRtosAssert(__FILE__, __LINE__);
}

void vApplicationGetIdleTaskMemory(StaticTask_t **tcb, StackType_t **stack, uint32_t *words)
{
    *tcb = &idle_control;
    *stack = idle_stack;
    *words = configMINIMAL_STACK_SIZE;
}

/* Inspect these in a debugger after an assertion or stack failure. */
const char * volatile BmsRtosFaultFile;
volatile unsigned BmsRtosFaultLine;
void BmsRtosAssert(const char *file, unsigned line)
{
    __disable_irq();
    BmsRtosFaultFile = file;
    BmsRtosFaultLine = line;
    for (;;) { __NOP(); }
}

void vApplicationStackOverflowHook(TaskHandle_t task, char *name)
{
    (void)task;
    BmsRtosAssert(name, 0);
}

void vApplicationMallocFailedHook(void)
{
    BmsRtosAssert("allocation failure", 0);
}
