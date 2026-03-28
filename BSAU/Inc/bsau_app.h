/*
 * @file    bsau_app.h
 * @brief   BSAU application module — public interface and master configuration.
 * @author  bugrASl
 *
 * Exposes exactly two functions called from main.c USER CODE sections.
 * In test mode, BSAU_Init() and BSAU_Run() delegate internally to the
 * test harness via #if guards in bsau_app.c — main.c never changes.
 */

#ifndef BSAU_APP_H
#define BSAU_APP_H

#ifdef __cplusplus
extern "C" {
#endif

/*============== ROLE DEFINITIONS ================================================================*/
#ifndef NRF_ROLE_TX
  #define NRF_ROLE_TX
#endif
#ifndef WL_ROLE_TX
  #define WL_ROLE_TX
#endif
/*==========================================================================================================*/

/*============== ADC/DMA PARAMETERS ======================================================================*/
/*
 * The ADC scan sequence contains 7 channels per trigger event:
 * Ranks 1-6  → 6 EMG channels (PA0-PA5, IN5-IN10)
 * Rank  7    → 1 battery channel (PB0, IN15)
 */
#define ADC_DMA_CHANNELS    7
#define ADC_DMA_SAMPLES     3
#define ADC_DMA_BUF_SIZE    (ADC_DMA_CHANNELS * ADC_DMA_SAMPLES)  /* = 21 */
#define ADC_BATT_INDEX      6
/*==========================================================================================================*/

/*============== RADIO LINK PARAMETERS ====================================================================*/
#define NRF_CHANNEL         76
#define NRF_ADDRESS         { 0xE7, 0xE7, 0xE7, 0xE7, 0xE7 }
/*==========================================================================================================*/

/*============== BATTERY LOW THRESHOLD ====================================================================*/
/* 100k/100k divider on PB0. At V_batt = 3.0V: (1.5/3.3) × 4095 ≈ 1861 */
#define BATT_LOW_THRESHOLD  1861
/*==========================================================================================================*/

/*============== GOERTZEL DFT CONFIG =====================================================================*/
#define GOERTZEL_NUM_BINS   5
#define GOERTZEL_BINS_HZ    { 50, 100, 200, 350, 500 }
#define GOERTZEL_BLOCK_SIZE 512
#define GOERTZEL_FS_HZ      2000.0f
/*==========================================================================================================*/

/*============== CONSIDERED CHANNELS =====================================================================*/
#define CONSIDERED_CHANNELS_COUNT   6
#define CONSIDERED_CHANNELS         { 0, 1, 2, 3, 4, 5 }
/*==========================================================================================================*/

/*============== API =============================================================================*/

void BSAU_Init(void);
void BSAU_Run(void);
/*==========================================================================================================*/

#ifdef __cplusplus
}
#endif

#endif /* BSAU_APP_H */
