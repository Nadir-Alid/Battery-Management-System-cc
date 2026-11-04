/*
 * Hybrid State of Charge estimation module.
 * Uses hardware coulomb counting with Dynamic Constant-Power tracking
 * for the buck converter, augmented by NMC OCV seeding.
 */

#ifndef COULOMB_COUNTER_H_
#define COULOMB_COUNTER_H_

#include <stdint.h>
#include <stdbool.h>
#include "bq76922_driver.h"

typedef struct {
    int32_t  passq_int;
    uint32_t passq_frac;
    uint32_t passq_time_s;

    float    exact_passq_mah;
    float    effective_passq_mah;
    float    soc_percent;
    float    max_capacity_mah;

    bool     is_calibrated;
    bool     rebase_pending;
    bool     full_candidate;
    uint32_t full_candidate_tick;

    float    passq_offset_mah;
    uint32_t last_passq_time_s;
    float    accumulated_invisible_mah;
} CoulombCounter_t;

void CC_Init(CoulombCounter_t *cc, float max_capacity);
BQ_Status_t CC_Update(CoulombCounter_t *cc, BQ_Handle_t *hbq);
/* Preserve estimated remaining charge and rebase after a successful hardware reset. */
BQ_Status_t CC_Reset(CoulombCounter_t *cc, BQ_Handle_t *hbq);

#endif /* COULOMB_COUNTER_H_ */
