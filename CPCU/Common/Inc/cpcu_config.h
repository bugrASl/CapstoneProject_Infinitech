/*
 * @file    cpcu_config.h
 * @brief   CPCU build mode selection — single point of configuration.
 * @author  bugrASl
 *
 * Uncomment exactly ONE mode below. Rebuild BOTH CM7 and CM4 projects.
 * log.h enforces mutual exclusion at compile time.
 *
 * CPCU_MODE_RELEASE     LOG=off  CSV=off   Production. Zero debug overhead.
 * CPCU_MODE_DEBUG       LOG=on   CSV=on    Dev (LOG events + CSV for SerialPlot).
 * CPCU_MODE_TEST_LOG    LOG=on   CSV=off   Unit/integration tests (LOG output).
 * CPCU_MODE_TEST_CSV    LOG=off  CSV=on    SerialPlot CSV-only capture.
 */

#ifndef CPCU_CONFIG_H
#define CPCU_CONFIG_H

/*============== BOARD IDENTITY (do not change) =============================*/
#define LOG_BOARD_CPCU
/*===========================================================================*/

/*============== SELECT EXACTLY ONE MODE ====================================*/

/* #define CPCU_MODE_RELEASE */
#define CPCU_MODE_DEBUG
/* #define CPCU_MODE_TEST_LOG */
/* #define CPCU_MODE_TEST_CSV */

/*===========================================================================*/

#endif /* CPCU_CONFIG_H */
