#ifndef BQ76922_DRIVER_H_
#define BQ76922_DRIVER_H_

#include "stm32f4xx_hal.h"
#include "bq76922.h"
#include <stdint.h>

#define BQ_DEV_ADDR          0x10U
#define BQ_I2C_TIMEOUT_MS    10U
#define BQ_SUBCMD_ADDR       0x3EU
#define BQ_SUBCMD_BUF        0x40U
#define BQ_CHECKSUM_ADDR     0x60U
#define BQ_SUBCMD_DELAY_US   2000U

typedef enum {
    BQ_OK       = 0,
    BQ_ERROR    = 1,
    BQ_BUSY     = 2,
    BQ_TIMEOUT  = 3
} BQ_Status_t;

typedef struct {
    /* Compact mapping: indices 0, 1, 2 hold physical cells C1, C2, C5.
       Remaining entries are reserved; current is positive during charging.
       Voltages are mV, current mA, temperatures tenths of degrees Celsius. */
    uint16_t cell_mv[16];
    uint16_t stack_mv;
    uint16_t pack_mv;
    uint16_t ld_mv;
    int16_t  pack_current_ma;
    int16_t  int_temp_dc;
    int16_t  ts1_temp_dc;
    uint16_t cb_active_mask;
} BQ_Telemetry_t;

typedef struct {
    uint16_t alarm_status;
    uint16_t safety_a;
    uint16_t safety_b;
    uint16_t safety_c;
    uint16_t pf_a;
    uint16_t pf_b;
    uint16_t pf_c;
    uint16_t pf_d;
    uint8_t  fet_status;

    uint8_t  uv_fault;
    uint8_t  ov_fault;
    uint8_t  ocd_fault;
    uint8_t  scd_fault;

    uint8_t  chg_on;
    uint8_t  pchg_on;
    uint8_t  dsg_on;
    uint8_t  pdsg_on;

    uint8_t  protections_triggered;
} BQ_Faults_t;

typedef struct {
    I2C_HandleTypeDef *hi2c;
    BQ_Telemetry_t     telem;
    BQ_Faults_t        faults;
} BQ_Handle_t;

BQ_Status_t BQ_Init(BQ_Handle_t *hbq, I2C_HandleTypeDef *hi2c);
BQ_Status_t BQ_ReadReg(BQ_Handle_t *hbq, uint8_t reg_addr, uint8_t *buf, uint8_t len);
BQ_Status_t BQ_WriteReg(BQ_Handle_t *hbq, uint8_t reg_addr, const uint8_t *buf, uint8_t len);
BQ_Status_t BQ_DirectRead(BQ_Handle_t *hbq, uint8_t cmd, uint16_t *out_raw);
BQ_Status_t BQ_DirectWrite(BQ_Handle_t *hbq, uint8_t cmd, uint16_t data);
BQ_Status_t BQ_Subcommand(BQ_Handle_t *hbq, uint16_t subcmd);
BQ_Status_t BQ_SubcommandRead(BQ_Handle_t *hbq, uint16_t subcmd, uint8_t *buf, uint8_t len);

BQ_Status_t BQ_ReadCellVoltage(BQ_Handle_t *hbq, uint8_t cmd, uint16_t *mv_out);
BQ_Status_t BQ_ReadBusVoltages(BQ_Handle_t *hbq, uint16_t *stack_mv, uint16_t *pack_mv, uint16_t *ld_mv);
BQ_Status_t BQ_ReadAllVoltages(BQ_Handle_t *hbq);
BQ_Status_t BQ_ReadCurrent(BQ_Handle_t *hbq, int16_t *ma_out);
BQ_Status_t BQ_ReadTemperature(BQ_Handle_t *hbq, uint8_t cmd, int16_t *dc_out);
BQ_Status_t BQ_ReadBalancing(BQ_Handle_t *hbq);

BQ_Status_t BQ_ReadAlarmStatus(BQ_Handle_t *hbq, uint16_t *out);
BQ_Status_t BQ_ClearAlarmBits(BQ_Handle_t *hbq, uint16_t mask);
BQ_Status_t BQ_ReadSafetyStatus(BQ_Handle_t *hbq);
BQ_Status_t BQ_ReadPFStatus(BQ_Handle_t *hbq);
BQ_Status_t BQ_ReadFETStatus(BQ_Handle_t *hbq);

BQ_Status_t BQ_Poll(BQ_Handle_t *hbq);
BQ_Status_t BQ_ReadAllTelemetry(BQ_Handle_t *hbq);

BQ_Status_t BQ_FETEnable(BQ_Handle_t *hbq);
BQ_Status_t BQ_AllFETsOff(BQ_Handle_t *hbq);
BQ_Status_t BQ_Shutdown(BQ_Handle_t *hbq);
BQ_Status_t BQ_PFReset(BQ_Handle_t *hbq);
BQ_Status_t BQ_OCDLRecover(BQ_Handle_t *hbq);
BQ_Status_t BQ_SCDLRecover(BQ_Handle_t *hbq);

#endif /* BQ76922_DRIVER_H_ */
