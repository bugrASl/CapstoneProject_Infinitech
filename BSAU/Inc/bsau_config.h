/*
 * @file    bsau_config.h
 * @brief   BSAU build mode selection — single point of configuration.
 * @author  bugrASl
 *
 * Uncomment exactly ONE mode below. Rebuild.
 * log.h enforces mutual exclusion at compile time.
 *
 * BSAU_MODE_RELEASE        LOG=off  CSV=off   Production.
 * BSAU_MODE_DEBUG          LOG=on   CSV=on    Dev (LOG stats + decimated CSV).
 * BSAU_MODE_TEST_ADC_CSV   LOG=off  CSV=on    Binary ADC stream (SerialPlot).
 * BSAU_MODE_TEST_PKT_LOG   LOG=on   CSV=off   WL codec round-trip verify.
 * BSAU_MODE_TEST_CSV       LOG=off  CSV=on    ASCII CSV capture.
 * BSAU_MODE_TEST_DFT_LOG   LOG=on   CSV=off   Goertzel DFT frequency verify.
 * BSAU_MODE_TEST_NRF_LOG   LOG=on   CSV=off   NRF self-test + TX loop.
 */

#ifndef BSAU_CONFIG_H
#define BSAU_CONFIG_H

/*============== BOARD IDENTITY (do not change) =============================*/
#define LOG_BOARD_BSAU
/*===========================================================================*/

/*============== SELECT EXACTLY ONE MODE ====================================*/

/* #define BSAU_MODE_RELEASE */
#define BSAU_MODE_DEBUG
/* #define BSAU_MODE_TEST_ADC_CSV */
/* #define BSAU_MODE_TEST_PKT_LOG */
/* #define BSAU_MODE_TEST_CSV */
/* #define BSAU_MODE_TEST_DFT_LOG */
/* #define BSAU_MODE_TEST_NRF_LOG */

/*===========================================================================*/

#endif /* BSAU_CONFIG_H */
