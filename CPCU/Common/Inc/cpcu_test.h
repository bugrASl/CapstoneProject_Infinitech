/*
 * @file    cpcu_test.h
 * @brief   On-target test harness for CPCU subsystems
 * @author  bugrASl
 *
 * Rev 6.0: TEST_LOG/TEST_CSV split, NRF test integration on CM4,
 *          hybrid real/synth loop, loosened timeouts, radio state machine test.
 */

#ifndef CPCU_TEST_H
#define CPCU_TEST_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

/*============== TEST RESULT TYPE ======================================================================*/
typedef enum {
    TEST_PASS = 0,
    TEST_FAIL = 1,
    TEST_SKIP = 2
} TestResult;
/*======================================================================================================*/

/*============== CM7 TEST ENTRY POINTS =================================================================*/
void        CPCU_CM7_TestMain(void);                    /* Never returns. Runs suite then monitor.   */
TestResult  CPCU_Test_WirelessPacket(void);             /* WL codec roundtrip (CM7 only)             */
TestResult  CPCU_Test_IPC(void);                        /* Ring buffer push/pop/overflow             */
void        CPCU_Test_Monitor(void);                    /* CSV stream for SerialPlot                 */
/*======================================================================================================*/

/*============== CM4 TEST ENTRY POINT ==================================================================*/
void        CPCU_CM4_TestMain(void);                    /* Never returns. NRF tests + hybrid loop. */
/*======================================================================================================*/

/*============== TEST CONFIGURATION (loosened Rev 6.0) =================================================*/
 
/* Synthetic packet rate when no BSAU is connected — loosened from 300 to 500 ms */
#define TEST_SYNTH_INTERVAL_MS      500
 
/* How long to wait for a real NRF packet before falling back to synthetic */
#define TEST_REAL_PKT_WINDOW_MS     500
 
/* Monitor CSV lines to capture before stopping */
#define TEST_MONITOR_CSV_LINES      100
 
/* Monitor diagnostic print interval */
#define TEST_MONITOR_DIAG_MS        1000
 
/* IPC overflow test: push this many to force overflow on 127-capacity ring */
#define TEST_IPC_OVERFLOW_COUNT     (IPC_SENSOR_RING_SIZE + 1) 
/*==========================================================================================================*/

#ifdef __cplusplus
}
#endif
 
#endif /* CPCU_TEST_H */
