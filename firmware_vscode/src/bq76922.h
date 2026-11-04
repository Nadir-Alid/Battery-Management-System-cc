/*
 * bq76922.h
 *
 *  Created on: Apr 6, 2026
 *      Author: ldnad
 */

#ifndef BQ76922_H_
#define BQ76922_H_

/* ==================================================================== */
/* DIRECT COMMANDS (Fast Polling & Interrupt Handling)                  */
/* ==================================================================== */

// System & Pin Status
#define ControlStatus     0x00
#define FETStatus         0x7F
#define LDPinVoltage      0x38

// Interrupt & Fault Diagnostics
#define AlarmStatus       0x62
#define SafetyStatusA     0x03
#define SafetyStatusB     0x05
#define SafetyStatusC     0x07
#define PFStatusA         0x0B
#define PFStatusB         0x0D
#define PFStatusC         0x0F
#define PFStatusD         0x11

// Live Telemetry (Voltage, Current, Temp)
#define StackVoltage      0x34
#define PACKPinVoltage    0x36
#define Cell1Voltage      0x14
#define Cell2Voltage      0x16
#define Cell5Voltage      0x1C
#define CC2Current        0x3A
#define IntTemperature    0x68
#define TS1Temperature    0x70


/* ==================================================================== */
/* SUBCOMMANDS (Data Returning - Read from 0x40 Buffer)                 */
/* ==================================================================== */

// Coulomb Counting & Balancing
#define DASTATUS6         0x0076 // Contains Accumulated Charge & Time
#define CB_ACTIVE_CELLS   0x0083


/* ==================================================================== */
/* COMMAND-ONLY SUBCOMMANDS (State Overrides - Written to 0x3E)         */
/* ==================================================================== */

// Power & Hardware Overrides
#define SHUTDOWN          0x0010
#define FET_ENABLE        0x0022
#define ALL_FETS_OFF      0x0095
#define RESET_PASSQ       0x0082 // Zeros out the coulomb counter

// Latch & Permanent Fail Clear Commands
#define PF_RESET          0x0029
#define OCDL_RECOVER      0x009B
#define SCDL_RECOVER      0x009C

#endif /* BQ76922_H_ */
