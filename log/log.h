/*
 * @file    log.h
 * @brief   Unified debug logging for CPCU (dual-core H7) and BSAU (single-core)
 * @author  bugrASl
 */

#ifndef LOG_H
#define LOG_H

#ifdef __cplusplus
extern "C" {
#endif

/* ================================================================
 * §1  BOARD IDENTITY VALIDATION
 * ================================================================ */

#if defined(LOG_BOARD_CPCU) && defined(LOG_BOARD_BSAU)
  #error "Define exactly one of LOG_BOARD_CPCU or LOG_BOARD_BSAU."
#endif

#if !defined(LOG_BOARD_CPCU) && !defined(LOG_BOARD_BSAU)
  #error "Define LOG_BOARD_CPCU or LOG_BOARD_BSAU in preprocessor settings."
#endif

/* ================================================================
 * §2  DERIVE LOG_ENABLED / LOG_CSV_ENABLED
 * ================================================================ */

/* ---- CPCU ---- */
#ifdef LOG_BOARD_CPCU

  #if (defined(CPCU_MODE_RELEASE) + defined(CPCU_MODE_DEBUG) + \
       defined(CPCU_MODE_TEST_LOG) + defined(CPCU_MODE_TEST_CSV)) > 1
    #error "Define exactly one CPCU_MODE_*."
  #endif

  #if !defined(CPCU_MODE_RELEASE) && !defined(CPCU_MODE_DEBUG) && \
      !defined(CPCU_MODE_TEST_LOG) && !defined(CPCU_MODE_TEST_CSV)
    #define CPCU_MODE_DEBUG
  #endif

  #if defined(CPCU_MODE_DEBUG)
    #define LOG_ENABLED     1
    #define LOG_CSV_ENABLED 1
  #elif defined(CPCU_MODE_TEST_LOG)
    #define LOG_ENABLED     1
    #define LOG_CSV_ENABLED 0
  #elif defined(CPCU_MODE_TEST_CSV)
    #define LOG_ENABLED     0
    #define LOG_CSV_ENABLED 1
  #else /* RELEASE */
    #define LOG_ENABLED     0
    #define LOG_CSV_ENABLED 0
  #endif

  #define LOG_TAG "CPCU"

#endif /* LOG_BOARD_CPCU */

/* ---- BSAU ---- */
#ifdef LOG_BOARD_BSAU

  /* Mode count validation */
  #if (defined(BSAU_MODE_RELEASE)      + defined(BSAU_MODE_DEBUG)        + \
       defined(BSAU_MODE_TEST_ADC_CSV) + defined(BSAU_MODE_TEST_PKT_LOG) + \
       defined(BSAU_MODE_TEST_CSV)     + defined(BSAU_MODE_TEST_DFT_LOG) + \
       defined(BSAU_MODE_TEST_NRF_LOG)) > 1
    #error "Define exactly one BSAU_MODE_*."
  #endif

  /* No default for BSAU — force explicit choice */
  #if !defined(BSAU_MODE_RELEASE)      && !defined(BSAU_MODE_DEBUG)        && \
      !defined(BSAU_MODE_TEST_ADC_CSV) && !defined(BSAU_MODE_TEST_PKT_LOG) && \
      !defined(BSAU_MODE_TEST_CSV)     && !defined(BSAU_MODE_TEST_DFT_LOG) && \
      !defined(BSAU_MODE_TEST_NRF_LOG)
    #error "Define exactly one BSAU_MODE_* in preprocessor settings."
  #endif

  /* Derive flags */
  #if defined(BSAU_MODE_DEBUG)
    #define LOG_ENABLED     1
    #define LOG_CSV_ENABLED 1
  #elif defined(BSAU_MODE_TEST_PKT_LOG) || defined(BSAU_MODE_TEST_DFT_LOG) || defined(BSAU_MODE_TEST_NRF_LOG)
    #define LOG_ENABLED     1
    #define LOG_CSV_ENABLED 0
  #elif defined(BSAU_MODE_TEST_ADC_CSV) || defined(BSAU_MODE_TEST_CSV)
    #define LOG_ENABLED     0
    #define LOG_CSV_ENABLED 1
  #else /* RELEASE */
    #define LOG_ENABLED     0
    #define LOG_CSV_ENABLED 0
  #endif

  #define LOG_TAG "BSAU"

#endif /* LOG_BOARD_BSAU */

/* ================================================================
 * §3  TRANSPORT PRIMITIVES
 * ================================================================ */

#if LOG_ENABLED || LOG_CSV_ENABLED

#include <stdio.h>

#ifdef LOG_BOARD_CPCU
  #include "cpcu_ipc.h"
  #define LOG_LOCK()    do { while (HAL_HSEM_FastTake(IPC_HSEM_UART) != HAL_OK) {} } while (0)
  #define LOG_UNLOCK()  HAL_HSEM_Release(IPC_HSEM_UART, 0)
#endif

#ifdef LOG_BOARD_BSAU
  #include <string.h>
  #include "usart.h"
  #define LOG_LOCK()    ((void)0)
  #define LOG_UNLOCK()  ((void)0)
#endif

#endif

/* ================================================================
 * §4  LOG — structured [BOARD - MOD]: func [STAT] message
 *
 * BSAU UART timeout: 20 ms
 *   At 115200 baud → 11520 bytes/sec → 86.8 µs/byte.
 *   LOG_BUF_SIZE = 160 chars → worst case 13.9 ms.
 *   20 ms gives ~44% headroom over worst case.
 * ================================================================ */

#if LOG_ENABLED

  #ifdef LOG_BOARD_CPCU

  #define LOG(mod, fn, stat, fmt, ...) \
      do { \
          LOG_LOCK(); \
          printf("[" LOG_TAG " - %-4s]: %-18s [%-4s]", (mod), (fn), (stat)); \
          if ((fmt)[0] != '\0') printf(" " fmt, ##__VA_ARGS__); \
          printf("\r\n"); \
          LOG_UNLOCK(); \
      } while (0)

  #endif /* LOG_BOARD_CPCU */

  #ifdef LOG_BOARD_BSAU

  #define LOG_BUF_SIZE  160

  #define LOG(mod, fn, stat, fmt, ...) \
      do { \
          char _lb[LOG_BUF_SIZE]; \
          int  _ln; \
          if ((fmt)[0] != '\0') \
              _ln = snprintf(_lb, sizeof(_lb), \
                  "[" LOG_TAG " - %-4s]: %-18s [%-4s] " fmt "\r\n", \
                  (mod), (fn), (stat), ##__VA_ARGS__); \
          else \
              _ln = snprintf(_lb, sizeof(_lb), \
                  "[" LOG_TAG " - %-4s]: %-18s [%-4s]\r\n", \
                  (mod), (fn), (stat)); \
          if (_ln > 0) \
              HAL_UART_Transmit(&huart1, (uint8_t *)_lb, (uint16_t)_ln, 20); \
      } while (0)

  #endif /* LOG_BOARD_BSAU */

#else /* LOG_ENABLED == 0 */

  #define LOG(mod, fn, stat, fmt, ...)  ((void)0)

#endif /* LOG_ENABLED */

/* ================================================================
 * §5  LOG_CSV — raw prefixless CSV data
 *
 * BSAU UART timeout: 15 ms
 *   LOG_CSV_BUF_SIZE = 128 chars → worst case 11.1 ms.
 *   15 ms gives ~35% headroom.
 *   Buffer raised from 96 to 128 to accommodate expanded CSV
 *   lines with dropped-packet counter column.
 * ================================================================ */

#if LOG_CSV_ENABLED

  #ifdef LOG_BOARD_CPCU

  #define LOG_CSV(fmt, ...) \
      do { \
          LOG_LOCK(); \
          printf(fmt "\r\n", ##__VA_ARGS__); \
          LOG_UNLOCK(); \
      } while (0)

  #endif /* LOG_BOARD_CPCU */

  #ifdef LOG_BOARD_BSAU

  #define LOG_CSV_BUF_SIZE  128

  #define LOG_CSV(fmt, ...) \
      do { \
          char _cb[LOG_CSV_BUF_SIZE]; \
          int  _cn = snprintf(_cb, sizeof(_cb), fmt "\r\n", ##__VA_ARGS__); \
          if (_cn > 0) \
              HAL_UART_Transmit(&huart1, (uint8_t *)_cb, (uint16_t)_cn, 15); \
      } while (0)

  #endif /* LOG_BOARD_BSAU */

#else /* LOG_CSV_ENABLED == 0 */

  #define LOG_CSV(fmt, ...)  ((void)0)

#endif /* LOG_CSV_ENABLED */

/* ================================================================ */

#ifdef __cplusplus
}
#endif

#endif /* LOG_H */
