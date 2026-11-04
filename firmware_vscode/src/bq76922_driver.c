#include "bq76922_driver.h"
#include <string.h>

extern void delayUS(uint32_t us);

static inline BQ_Status_t hal_to_bq(HAL_StatusTypeDef s) {
    switch (s) {
        case HAL_OK:      return BQ_OK;
        case HAL_BUSY:    return BQ_BUSY;
        case HAL_TIMEOUT: return BQ_TIMEOUT;
        default:          return BQ_ERROR;
    }
}

BQ_Status_t BQ_Init(BQ_Handle_t *hbq, I2C_HandleTypeDef *hi2c) {
    if (hbq == NULL || hi2c == NULL) return BQ_ERROR;
    hbq->hi2c = hi2c;
    memset(&hbq->telem,  0, sizeof(hbq->telem));
    memset(&hbq->faults, 0, sizeof(hbq->faults));
    return BQ_OK;
}

BQ_Status_t BQ_ReadReg(BQ_Handle_t *hbq, uint8_t reg_addr, uint8_t *buf, uint8_t len) {
    if (hbq == NULL || hbq->hi2c == NULL || buf == NULL || len == 0) return BQ_ERROR;
    HAL_StatusTypeDef ret = HAL_I2C_Mem_Read(hbq->hi2c, BQ_DEV_ADDR, reg_addr, I2C_MEMADD_SIZE_8BIT, buf, len, BQ_I2C_TIMEOUT_MS);
    return hal_to_bq(ret);
}

BQ_Status_t BQ_WriteReg(BQ_Handle_t *hbq, uint8_t reg_addr, const uint8_t *buf, uint8_t len) {
    if (hbq == NULL || hbq->hi2c == NULL || buf == NULL || len == 0) return BQ_ERROR;
    HAL_StatusTypeDef ret = HAL_I2C_Mem_Write(hbq->hi2c, BQ_DEV_ADDR, reg_addr, I2C_MEMADD_SIZE_8BIT, (uint8_t *)buf, len, BQ_I2C_TIMEOUT_MS);
    return hal_to_bq(ret);
}

BQ_Status_t BQ_DirectRead(BQ_Handle_t *hbq, uint8_t cmd, uint16_t *out_raw) {
    if (out_raw == NULL) return BQ_ERROR;
    uint8_t rx[2] = {0, 0};
    BQ_Status_t s = BQ_ReadReg(hbq, cmd, rx, 2);
    if (s != BQ_OK) return s;
    *out_raw = (uint16_t)((rx[1] << 8) | rx[0]);
    return BQ_OK;
}

BQ_Status_t BQ_DirectWrite(BQ_Handle_t *hbq, uint8_t cmd, uint16_t data) {
    uint8_t tx[2];
    tx[0] = (uint8_t)(data & 0xFF);
    tx[1] = (uint8_t)((data >> 8) & 0xFF);
    return BQ_WriteReg(hbq, cmd, tx, 2);
}

BQ_Status_t BQ_Subcommand(BQ_Handle_t *hbq, uint16_t subcmd) {
    uint8_t tx[2];
    tx[0] = (uint8_t)(subcmd & 0xFF);
    tx[1] = (uint8_t)((subcmd >> 8) & 0xFF);
    BQ_Status_t s = BQ_WriteReg(hbq, BQ_SUBCMD_ADDR, tx, 2);
    if (s != BQ_OK) return s;
    delayUS(BQ_SUBCMD_DELAY_US);
    return BQ_OK;
}

BQ_Status_t BQ_SubcommandRead(BQ_Handle_t *hbq, uint16_t subcmd, uint8_t *buf, uint8_t len) {
    if (buf == NULL || len == 0 || len > 32U) return BQ_ERROR;
    BQ_Status_t s = BQ_Subcommand(hbq, subcmd);
    if (s != BQ_OK) return s;

    return BQ_ReadReg(hbq, BQ_SUBCMD_BUF, buf, len);
}

BQ_Status_t BQ_ReadCellVoltage(BQ_Handle_t *hbq, uint8_t cmd, uint16_t *mv_out) {
    if (mv_out == NULL) return BQ_ERROR;
    uint16_t raw = 0;
    BQ_Status_t s = BQ_DirectRead(hbq, cmd, &raw);
    if (s != BQ_OK) return s;
    *mv_out = raw;
    return BQ_OK;
}

BQ_Status_t BQ_ReadBusVoltages(BQ_Handle_t *hbq, uint16_t *stack_mv, uint16_t *pack_mv, uint16_t *ld_mv) {
    if (stack_mv == NULL || pack_mv == NULL || ld_mv == NULL) return BQ_ERROR;
    uint16_t raw = 0;
    BQ_Status_t s;

    // FIX: Removed the * 10U multiplier.
    // The BQ76922 is already returning these natively in millivolts.
    s = BQ_DirectRead(hbq, StackVoltage, &raw);
    if (s != BQ_OK) return s;
    *stack_mv = raw;

    s = BQ_DirectRead(hbq, PACKPinVoltage, &raw);
    if (s != BQ_OK) return s;
    *pack_mv = raw;

    s = BQ_DirectRead(hbq, LDPinVoltage, &raw);
    if (s != BQ_OK) return s;
    *ld_mv = raw;

    return BQ_OK;
}

BQ_Status_t BQ_ReadAllVoltages(BQ_Handle_t *hbq) {
    if (hbq == NULL) return BQ_ERROR;
    BQ_Status_t s;

    // FIX: We specifically target the 3 active cells on your hardware configuration.
    // This perfectly maps C1 to index [0], C2 to index [1], and C5 to index [2].
    const uint8_t cell_cmds[3] = { Cell1Voltage, Cell2Voltage, Cell5Voltage };

    for (uint8_t i = 0; i < 3U; i++) {
        s = BQ_ReadCellVoltage(hbq, cell_cmds[i], &hbq->telem.cell_mv[i]);
        if (s != BQ_OK) return s;
    }

    return BQ_ReadBusVoltages(hbq, &hbq->telem.stack_mv, &hbq->telem.pack_mv, &hbq->telem.ld_mv);
}

BQ_Status_t BQ_ReadCurrent(BQ_Handle_t *hbq, int16_t *ma_out) {
    if (ma_out == NULL) return BQ_ERROR;
    uint16_t raw = 0;
    BQ_Status_t s = BQ_DirectRead(hbq, CC2Current, &raw);
    if (s != BQ_OK) return s;
    *ma_out = (int16_t)raw;
    return BQ_OK;
}

BQ_Status_t BQ_ReadTemperature(BQ_Handle_t *hbq, uint8_t cmd, int16_t *dc_out) {
    if (dc_out == NULL) return BQ_ERROR;
    uint16_t raw = 0;
    BQ_Status_t s = BQ_DirectRead(hbq, cmd, &raw);
    if (s != BQ_OK) return s;
    *dc_out = (int16_t)((int32_t)raw - 2731);
    return BQ_OK;
}

BQ_Status_t BQ_ReadBalancing(BQ_Handle_t *hbq) {
    if (hbq == NULL) return BQ_ERROR;
    uint8_t buf[2] = {0};
    BQ_Status_t s = BQ_SubcommandRead(hbq, CB_ACTIVE_CELLS, buf, 2U);
    if (s != BQ_OK) return s;
    hbq->telem.cb_active_mask = (uint16_t)(buf[0] | (buf[1] << 8));
    return BQ_OK;
}

BQ_Status_t BQ_ReadAlarmStatus(BQ_Handle_t *hbq, uint16_t *out) {
    if (hbq == NULL) return BQ_ERROR;
    uint16_t raw = 0;
    BQ_Status_t s = BQ_DirectRead(hbq, AlarmStatus, &raw);
    if (s != BQ_OK) return s;
    hbq->faults.alarm_status = raw;
    if (out != NULL) *out = raw;
    return BQ_OK;
}

BQ_Status_t BQ_ClearAlarmBits(BQ_Handle_t *hbq, uint16_t mask) {
    return BQ_DirectWrite(hbq, AlarmStatus, mask);
}

BQ_Status_t BQ_ReadSafetyStatus(BQ_Handle_t *hbq) {
    if (hbq == NULL) return BQ_ERROR;
    uint16_t raw = 0;
    BQ_Status_t s;
    s = BQ_DirectRead(hbq, SafetyStatusA, &raw);
    if (s != BQ_OK) return s;
    hbq->faults.safety_a = raw;

    uint8_t lo = (uint8_t)(raw & 0xFF);
    uint8_t hi = (uint8_t)((raw >> 8) & 0xFF);
    hbq->faults.uv_fault  = (lo >> 2) & 0x01U;
    hbq->faults.ov_fault  = (lo >> 3) & 0x01U;
    hbq->faults.ocd_fault = (hi >> 1) & 0x01U;
    hbq->faults.scd_fault = (hi >> 3) & 0x01U;

    s = BQ_DirectRead(hbq, SafetyStatusB, &raw);
    if (s != BQ_OK) return s;
    hbq->faults.safety_b = raw;

    s = BQ_DirectRead(hbq, SafetyStatusC, &raw);
    if (s != BQ_OK) return s;
    hbq->faults.safety_c = raw;

    hbq->faults.protections_triggered = (hbq->faults.safety_a != 0U || hbq->faults.safety_b != 0U || hbq->faults.safety_c != 0U) ? 1U : 0U;
    return BQ_OK;
}

BQ_Status_t BQ_ReadPFStatus(BQ_Handle_t *hbq) {
    if (hbq == NULL) return BQ_ERROR;
    uint16_t raw = 0;
    BQ_Status_t s;
    s = BQ_DirectRead(hbq, PFStatusA, &raw);
    if (s != BQ_OK) return s;
    hbq->faults.pf_a = raw;
    s = BQ_DirectRead(hbq, PFStatusB, &raw);
    if (s != BQ_OK) return s;
    hbq->faults.pf_b = raw;
    s = BQ_DirectRead(hbq, PFStatusC, &raw);
    if (s != BQ_OK) return s;
    hbq->faults.pf_c = raw;
    s = BQ_DirectRead(hbq, PFStatusD, &raw);
    if (s != BQ_OK) return s;
    hbq->faults.pf_d = raw;
    return BQ_OK;
}

BQ_Status_t BQ_ReadFETStatus(BQ_Handle_t *hbq) {
    if (hbq == NULL) return BQ_ERROR;
    uint16_t raw = 0;
    BQ_Status_t s = BQ_DirectRead(hbq, FETStatus, &raw);
    if (s != BQ_OK) return s;
    uint8_t lo = (uint8_t)(raw & 0xFF);
    hbq->faults.fet_status = lo;
    hbq->faults.chg_on  = (lo >> 0) & 0x01U;
    hbq->faults.pchg_on = (lo >> 1) & 0x01U;
    hbq->faults.dsg_on  = (lo >> 2) & 0x01U;
    hbq->faults.pdsg_on = (lo >> 3) & 0x01U;
    return BQ_OK;
}

BQ_Status_t BQ_Poll(BQ_Handle_t *hbq) {
    if (hbq == NULL) return BQ_ERROR;
    uint16_t alarm = 0;
    BQ_Status_t s = BQ_ReadAlarmStatus(hbq, &alarm);
    if (s != BQ_OK) return s;

    // FIX: We bypass the `if (alarm & 0x0080U)` FULLSCAN check entirely because
    // it was permanently masked off in your OTP file. The driver now unconditionally
    // updates the live telemetry fields to keep the display completely responsive.
    s = BQ_ReadAllVoltages(hbq);
    if (s != BQ_OK) return s;
    s = BQ_ReadCurrent(hbq, &hbq->telem.pack_current_ma);
    if (s != BQ_OK) return s;
    s = BQ_ReadTemperature(hbq, IntTemperature, &hbq->telem.int_temp_dc);
    if (s != BQ_OK) return s;
    s = BQ_ReadTemperature(hbq, TS1Temperature, &hbq->telem.ts1_temp_dc);
    if (s != BQ_OK) return s;

    // If the FULLSCAN bit happens to appear, we clear it so it doesn't get stuck
    if (alarm & 0x0080U) {
        s = BQ_ClearAlarmBits(hbq, 0x0080U);
        if (s != BQ_OK) return s;
    }

    // Safety Fault extraction
    if (alarm & 0xC000U) {
        s = BQ_ReadSafetyStatus(hbq);
        if (s != BQ_OK) return s;
        s = BQ_ClearAlarmBits(hbq, 0xF800U);
        if (s != BQ_OK) return s;
    } else if (hbq->faults.protections_triggered) {
        s = BQ_ReadSafetyStatus(hbq);
        if (s != BQ_OK) return s;
    }

    return BQ_OK;
}

BQ_Status_t BQ_ReadAllTelemetry(BQ_Handle_t *hbq) {
    if (hbq == NULL) return BQ_ERROR;
    BQ_Status_t s;
    s = BQ_ReadAllVoltages(hbq);
    if (s != BQ_OK) return s;
    s = BQ_ReadCurrent(hbq, &hbq->telem.pack_current_ma);
    if (s != BQ_OK) return s;
    s = BQ_ReadTemperature(hbq, IntTemperature, &hbq->telem.int_temp_dc);
    if (s != BQ_OK) return s;
    s = BQ_ReadTemperature(hbq, TS1Temperature, &hbq->telem.ts1_temp_dc);
    if (s != BQ_OK) return s;
    return BQ_OK;
}

BQ_Status_t BQ_FETEnable(BQ_Handle_t *hbq) { return BQ_Subcommand(hbq, FET_ENABLE); }
BQ_Status_t BQ_AllFETsOff(BQ_Handle_t *hbq) { return BQ_Subcommand(hbq, ALL_FETS_OFF); }
BQ_Status_t BQ_Shutdown(BQ_Handle_t *hbq) {
    BQ_Status_t s = BQ_Subcommand(hbq, SHUTDOWN);
    if (s != BQ_OK) return s;
    return BQ_Subcommand(hbq, SHUTDOWN);
}
BQ_Status_t BQ_PFReset(BQ_Handle_t *hbq) { return BQ_Subcommand(hbq, PF_RESET); }
BQ_Status_t BQ_OCDLRecover(BQ_Handle_t *hbq) { return BQ_Subcommand(hbq, OCDL_RECOVER); }
BQ_Status_t BQ_SCDLRecover(BQ_Handle_t *hbq) { return BQ_Subcommand(hbq, SCDL_RECOVER); }
