/*
 * @file    cpcu_cm4_app.h
 * @brief   CPCU CM4 Reflex Layer — public interface.
 * @author  bugrASl
 *
 *   CPCU_CM4_Init() — called once from USER CODE BEGIN 2 (after all MX_ inits)
 *   CPCU_CM4_Run()  — called from USER CODE BEGIN WHILE (never returns)
 *
 * The EXTI callback is a HAL weak-override — linked automatically.
 *
 * -- Build Mode Behavior ---------------------------------------------------
 *
 *   CPCU_MODE_RELEASE:   IPC + NRF + PWM init, silent reflex loop.
 *   CPCU_MODE_DEBUG:     Same + LOG on key events (pkts, cmds, state).
 *   CPCU_MODE_TEST_LOG:  Delegates to CPCU_CM4_TestMain() (NRF tests + hybrid).
 *   CPCU_MODE_TEST_CSV:  Same delegation, CSV-only output.
 */

#ifndef CPCU_CM4_APP_H
#define CPCU_CM4_APP_H

#ifdef __cplusplus
extern "C" {
#endif

#include "cpcu_ipc.h"
#include "nrf24l01.h"
#include <stdbool.h>

/*============== NRF RADIO CONFIGURATION ===================================================================*/
#define NRF_RF_CHANNEL          76
#define NRF_RX_ADDRESS          {0xE7, 0xE7, 0xE7, 0xE7, 0xE7}
/*==========================================================================================================*/

/*============== TIMING CONSTANTS (Rev 6.0 — loosened for robustness) ======================================*/

/* Radio timeout state machine */
#define RADIO_TIMEOUT_MS        750             /* RUNNING i-> DEGRADED threshold */
#define RECOVERY_PKT_COUNT      10              /* Consecutive OK before RUNNING */

/* NRF init retry */
#define NRF_INIT_MAX_RETRIES    3               /* Total attempts at boot */
#define NRF_INIT_POR_DELAY_MS   200             /* NRF POR spec=100ms + 100% margin */
#define NRF_INIT_BACKOFF_MS     100             /* Per-attempt base (×attempt) */
#define NRF_REINIT_INTERVAL_MS  3000            /* Re-init period while DEGRADED */

/* Link quality monitoring */
#define LINK_STATS_WINDOW_MS    1000            /* Rolling window for stats */
#define LINK_EXPECTED_PKT_RATE  667             /* Expected packets/sec at 2 kHz / 3 samp */

/* NRF register not in nrf24l01.h */
#define NRF_REG_RPD             0x09            /* Received Power Detector */
/*==========================================================================================================*/

/*============== PER-JOINT SERVO LIMITS (µs) ===============================================================*/

/*
 * SafetyClamp() constrains every motor command to these ranges.
 * Indexed by IPC_NUM_SERVOS (6 channels: TIM1 CH1-4, TIM8 CH1-2).
 */
#define SERVO_MIN_VALUES        {600,  600,  500,  600,  500,  700}
#define SERVO_MAX_VALUES        {2400, 2200, 2400, 2400, 2400, 2300}
/*==========================================================================================================*/

/*============== RADIO STATE MACHINE =======================================================================*/
typedef enum {
    RADIO_RUNNING,              /* Normal: servos active, NRF active, cmds accepted */
    RADIO_DEGRADED,             /* No pkts: servos neutral, NRF active, cmds blocked */
    RADIO_RECOVERING,           /* Pkts arriving: counting consecutive OK */
    RADIO_SAFE                  /* Permanent: servos neutral, NRF blocked, reset only */
} RadioState;
/*==========================================================================================================*/

/*============== EXTERN DECLARATIONS (for cpcu_test.c access) ==============================================*/

/*
 * These globals are defined in cpcu_cm4_app.c (non-static) so that
 * cpcu_test.c can run NRF self-tests and check real packet reception
 * during the hybrid test loop. Only accessed by test code.
 */
extern NRF_Handle    g_hnrf;
extern bool          g_nrf_init_ok;
extern volatile bool nrf_irq_flag;
/*==========================================================================================================*/

/*============== API =======================================================================================*/

void CPCU_CM4_Init(void);
void CPCU_CM4_Run(void);
/*==========================================================================================================*/

#ifdef __cplusplus
}
#endif

#endif /* CPCU_CM4_APP_H */
