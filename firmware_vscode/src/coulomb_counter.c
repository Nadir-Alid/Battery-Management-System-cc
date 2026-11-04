#include "coulomb_counter.h"
#include "bq76922.h"

#define NUM_SERIES_CELLS        3     // 3S (C1, C2, C5): 3 battery cells

// Boundary Thresholds (this is specific to the physical battery used)
#define NMC_CELL_FULL_MV        4150
#define NMC_TAPER_CURRENT_MA    200
#define NMC_CELL_EMPTY_MV       3000
#define NMC_FULL_CONFIRM_MS     30000U

/*
   1. NMC OCV LOOKUP TABLE
*/
static const uint16_t ocv_mv[]  = {3000, 3300, 3400, 3500, 3600, 3700, 3800, 3900, 4000, 4100, 4150};
static const float    ocv_soc[] = {   0,    5,   10,   18,   30,   42,   58,   72,   82,   92,  100};

static float Estimate_NMC_SOC_From_Voltage(uint16_t avg_cell_mv) {
    if (avg_cell_mv <= ocv_mv[0]) return ocv_soc[0];
    if (avg_cell_mv >= ocv_mv[10]) return ocv_soc[10];

    for (int i = 0; i < 10; i++) {
        if (avg_cell_mv >= ocv_mv[i] && avg_cell_mv <= ocv_mv[i+1]) {
            float v_range = (float)(ocv_mv[i+1] - ocv_mv[i]);
            float soc_range = ocv_soc[i+1] - ocv_soc[i];
            float fraction = (float)(avg_cell_mv - ocv_mv[i]) / v_range;
            return ocv_soc[i] + (fraction * soc_range);
        }
    }
    return 0.0f;
}

/* 
   2. EMPIRICAL HARDWARE LOAD LOOKUP TABLE
*/
static const uint16_t load_pack_mv[] = {7500,  8000,  8500,  9000,  9500, 10000, 10500, 11000, 11500, 12000, 12600};
static const float    load_draw_ma[] = {108.5, 102.8,  97.4,  92.2,  87.8,  84.1,  80.3,  77.1,  74.0,  71.1,  68.0};

static float Get_Dynamic_Invisible_Load_mA(uint16_t pack_mv) {
    if (pack_mv <= load_pack_mv[0]) return load_draw_ma[0];
    if (pack_mv >= load_pack_mv[10]) return load_draw_ma[10];

    for (int i = 0; i < 10; i++) {
        if (pack_mv >= load_pack_mv[i] && pack_mv <= load_pack_mv[i+1]) {
            float v_range = (float)(load_pack_mv[i+1] - load_pack_mv[i]);
            float i_range = load_draw_ma[i+1] - load_draw_ma[i];
            float fraction = (float)(pack_mv - load_pack_mv[i]) / v_range;
            return load_draw_ma[i] + (fraction * i_range);
        }
    }
    return 68.0f;
}

/*
   COULOMB COUNTER CORE LOGIC
*/

void CC_Init(CoulombCounter_t *cc, float max_capacity)
{
    if (cc == NULL) return;
    cc->passq_int = 0;
    cc->passq_frac = 0;
    cc->passq_time_s = 0;
    cc->effective_passq_mah = 0.0f;
    cc->exact_passq_mah = 0.0f;
    cc->soc_percent = 0.0f;
    cc->max_capacity_mah = max_capacity;
    cc->is_calibrated = false;
    cc->rebase_pending = false;
    cc->full_candidate = false;
    cc->full_candidate_tick = 0;
    cc->passq_offset_mah = 0.0f;
    cc->last_passq_time_s = 0;
    cc->accumulated_invisible_mah = 0.0f;
}

BQ_Status_t CC_Update(CoulombCounter_t *cc, BQ_Handle_t *hbq)
{
    if (cc == NULL || hbq == NULL || !(cc->max_capacity_mah > 0.0f)) return BQ_ERROR;
    uint8_t buf[32] = {0};

    BQ_Status_t s = BQ_SubcommandRead(hbq, DASTATUS6, buf, 12U);
    if (s != BQ_OK) {
        cc->full_candidate = false;
        return s;
    }

    cc->passq_int    = (int32_t)(((uint32_t)buf[3]  << 24) | ((uint32_t)buf[2]  << 16) | ((uint32_t)buf[1]  <<  8) | buf[0]);
    cc->passq_frac   = ((uint32_t)buf[7]  << 24) | ((uint32_t)buf[6]  << 16) | ((uint32_t)buf[5]  <<  8) | buf[4];
    cc->passq_time_s = ((uint32_t)buf[11] << 24) | ((uint32_t)buf[10] << 16) | ((uint32_t)buf[9]  <<  8) | buf[8];

    // Calculate High-Resolution Sub-mAh fractions (Divided by 2^32 as per TI TRM)
    cc->exact_passq_mah = (float)cc->passq_int + ((float)cc->passq_frac / 4294967296.0f);

    uint16_t min_cell = 0xFFFF;
    uint32_t sum_cell = 0;

    for (int i = 0; i < NUM_SERIES_CELLS; i++) {
        uint16_t v = hbq->telem.cell_mv[i];
        if (v == 0) { cc->full_candidate = false; return BQ_ERROR; }
        if (v < min_cell) min_cell = v;
        sum_cell += v;
    }
    uint16_t avg_cell_mv = sum_cell / NUM_SERIES_CELLS;

    /* 3. OCV BOOT SEEDING */
    if (!cc->is_calibrated) {
        float boot_soc = Estimate_NMC_SOC_From_Voltage(avg_cell_mv);
        cc->soc_percent = boot_soc;
        cc->effective_passq_mah = (cc->max_capacity_mah * boot_soc) / 100.0f;

        cc->passq_offset_mah = cc->effective_passq_mah - cc->exact_passq_mah;
        cc->last_passq_time_s = cc->passq_time_s;
        cc->accumulated_invisible_mah = 0.0f;

        cc->is_calibrated = true;
        cc->rebase_pending = false;
        return BQ_OK;
    }

    /* A reset is not a 32-bit timer rollover. Keep the last SOC estimate. */
    if (cc->rebase_pending ||
        (cc->passq_time_s < cc->last_passq_time_s &&
         !(cc->last_passq_time_s > UINT32_MAX - 60U && cc->passq_time_s < 60U))) {
        cc->passq_offset_mah = cc->effective_passq_mah - cc->exact_passq_mah;
        cc->accumulated_invisible_mah = 0.0f;
        cc->last_passq_time_s = cc->passq_time_s;
        cc->rebase_pending = false;
        cc->full_candidate = false;
        return BQ_OK;
    }

    /* 4. EMPIRICAL DYNAMIC LOAD ACCUMULATION */
    uint32_t time_delta_s = 0;
    if (cc->passq_time_s >= cc->last_passq_time_s) {
        time_delta_s = cc->passq_time_s - cc->last_passq_time_s;
    } else {
        time_delta_s = (0xFFFFFFFF - cc->last_passq_time_s) + cc->passq_time_s + 1;
    }
    cc->last_passq_time_s = cc->passq_time_s;

    if (time_delta_s > 0) {
        uint16_t pack_mv_total = avg_cell_mv * NUM_SERIES_CELLS;
        float dynamic_battery_draw_ma = Get_Dynamic_Invisible_Load_mA(pack_mv_total);
        float hours_elapsed = (float)time_delta_s / 3600.0f;
        cc->accumulated_invisible_mah += (dynamic_battery_draw_ma * hours_elapsed);
    }

    /* 5. AUTO-SYNCHRONIZATION */
    bool sync_triggered = false;

    bool full_now = min_cell >= NMC_CELL_FULL_MV &&
        hbq->telem.pack_current_ma > 0 && hbq->telem.pack_current_ma <= NMC_TAPER_CURRENT_MA;
    uint32_t now = HAL_GetTick();
    if (!full_now) {
        cc->full_candidate = false;
    } else if (!cc->full_candidate) {
        cc->full_candidate = true;
        cc->full_candidate_tick = now;
    }
    if (full_now && (uint32_t)(now - cc->full_candidate_tick) >= NMC_FULL_CONFIRM_MS) {
        cc->soc_percent = 100.0f;
        cc->effective_passq_mah = cc->max_capacity_mah;
        cc->accumulated_invisible_mah = 0.0f;
        cc->passq_offset_mah = cc->max_capacity_mah - cc->exact_passq_mah;
        sync_triggered = true;
    }
    else if (min_cell <= NMC_CELL_EMPTY_MV) {
        cc->soc_percent = 0.0f;
        cc->effective_passq_mah = 0.0f;
        cc->accumulated_invisible_mah = 0.0f;
        cc->passq_offset_mah = 0.0f - cc->exact_passq_mah;
        sync_triggered = true;
    }

    /* 6. STANDARD DEAD-RECKONING OPERATION */
    if (!sync_triggered) {
        cc->effective_passq_mah = cc->passq_offset_mah + cc->exact_passq_mah - cc->accumulated_invisible_mah;

        if (cc->effective_passq_mah < 0.0f) {
            cc->effective_passq_mah = 0.0f;
        } else if (cc->effective_passq_mah > cc->max_capacity_mah) {
            cc->effective_passq_mah = cc->max_capacity_mah;
        }

        if (cc->max_capacity_mah > 0) {
            cc->soc_percent = (cc->effective_passq_mah / cc->max_capacity_mah) * 100.0f;
        }
    }

    return BQ_OK;
}

BQ_Status_t CC_Reset(CoulombCounter_t *cc, BQ_Handle_t *hbq)
{
    if (cc == NULL || hbq == NULL) return BQ_ERROR;
    BQ_Status_t s = BQ_Subcommand(hbq, RESET_PASSQ);
    if (s == BQ_OK) {
        cc->rebase_pending = true;
        cc->full_candidate = false;
    }
    return s;
}
