/*
 * @file    cpcu_cm7_app.h
 * @brief   CPCU CM7 Cognitive Layer — public interface.
 * @author  bugrASl
 *
 * -- Build Mode Behavior ---------------------------------------------------
 *
 *   CPCU_MODE_RELEASE:   DWT setup, silent cognitive loop.
 *   CPCU_MODE_DEBUG:     Same + UART banner + periodic diagnostics.
 *   CPCU_MODE_TEST_LOG:  Delegates to CPCU_CM7_TestMain() (test suite + monitor).
 *   CPCU_MODE_TEST_CSV:  Same delegation, CSV-only output.
 */

#ifndef CPCU_CM7_APP_H
#define CPCU_CM7_APP_H

#ifdef __cplusplus
extern "C" {
#endif

/*============== THRESHOLD CLASSIFIER CONFIGURATION ========================================================*/

/*
 * DSP/ML placeholder: 3 thresholds divide each 12-bit ADC channel into
 * 4 zones. Each zone maps linearly to a fraction of servo travel.
 * Tune by watching CSV output in SerialPlot.
 *
 *   [0, THR_LOW)        → zone 0 (rest)   → 1500 µs (neutral)
 *   [THR_LOW, THR_MID)  → zone 1 (light)  → 1833 µs (1/3 travel)
 *   [THR_MID, THR_HIGH) → zone 2 (medium) → 2166 µs (2/3 travel)
 *   [THR_HIGH, 4095]    → zone 3 (strong) → 2500 µs (full grip)
 */
#define THR_LOW                 800
#define THR_MID                 1600
#define THR_HIGH                2800
/*==========================================================================================================*/    

/*============== TIMING ====================================================================================*/
#define CM7_DEBUG_INTERVAL_MS   1000            /* Diagnostic print period (1/sec) */
/*==========================================================================================================*/    

/*============== API ========================================================================================*/

void CPCU_CM7_Init(void);
void CPCU_CM7_Run(void);
/*==========================================================================================================*/    

#ifdef __cplusplus
}
#endif

#endif /* CPCU_CM7_APP_H */
