/*
 * @file    nrf24l01_test.h
 * @brief   On-target self-test suite for NRF24L01+ driver
 * @author  bugrASl
 *
 * Tests that can be run after NRF_Init() to verify the SPI chain,
 * register configuration, and RF state machine are functional.
 *
 * ── Test Hierarchy ──────────────────────────────────────────────
 *
 *   Test 1 — SPI Loopback:
 *     Write/readback on a scratch register (SETUP_AW).
 *     Catches: wiring faults, SPI clock polarity, dead chip.
 *
 *   Test 2 — Register Audit:
 *     Read back every register programmed by NRF_Init() and
 *     compare against expected values.
 *     Catches: init-order bugs, register write failures.
 *
 *   Test 3 — Address Verify:
 *     Read back the 5-byte pipe 0 address (and TX_ADDR for TX role).
 *     Catches: multi-byte SPI transfer issues.
 *
 *   Test 4 — FIFO Exercise:
 *     Verify FIFOs start empty, write a dummy payload, verify
 *     non-empty, flush, verify empty again.
 *     Catches: FIFO controller faults, SPI command issues.
 *
 *   Test 5 — Power Cycle:
 *     Power down → verify PWR_UP=0 → power up → verify PWR_UP=1.
 *     Catches: CONFIG register write failures, oscillator startup.
 *
 *   Test 6 (TX only) — RF State Machine:
 *     Transmit a test packet and check for TX_DS or MAX_RT.
 *     Both are valid — the key is the chip's state machine responds.
 *     MAX_RT = "no receiver" (expected if testing TX alone).
 *     Catches: RF synthesizer, PLL, CE pulse timing issues.
 *
 *   Test 7 (RX only) — Receive Readiness:
 *     Verify CONFIG has PRIM_RX set, CE is high, chip is in RX mode.
 *     Catches: init left the chip in wrong mode.
 *
 * ── Integration ─────────────────────────────────────────────────
 *
 *   Call NRF_Test_All() after NRF_Init() returns NRF_OK.
 *   It runs all applicable tests and returns TEST_PASS/TEST_FAIL.
 *   Each sub-test logs PASS/FAIL via LOG().
 *
 *   BSAU: call from BSAU_Test_Init() or BSAU_Init() in DEBUG/TEST_NRF_LOG mode.
 *   CPCU: call from CPCU_CM4_TestMain() (NRF is on CM4 side).
 */
 
#ifndef NRF24L01_TEST_H
#define NRF24L01_TEST_H
 
#ifdef __cplusplus
extern "C" {
#endif
 
#include "nrf24l01.h"

/*============== TestResult — guarded for CPCU coexistence =================================================
 * On CPCU, cpcu_test.h defines TestResult (with TEST_SKIP = 2) and its
 * include guard is CPCU_TEST_H. If cpcu_test.h is included before this
 * header, skip our definition to avoid a redefinition error.
 * On BSAU, CPCU_TEST_H is never defined so this always compiles.
 * =========================================================================================================*/
#ifndef CPCU_TEST_H
typedef enum {
    TEST_PASS = 0,
    TEST_FAIL = 1
} TestResult;
#endif
/*==========================================================================================================*/

/*============== RX TEST TIMEOUT ===========================================================================
 * NRF_TEST_RX_TIMEOUT_MS controls Phase B of the RX test:
 *   > 0  → poll STATUS for RX_DR up to this many ms, then FAIL if no packet
 *   = 0  → skip Phase B entirely (only run config checks)
 *
 * Default: 5000 ms (5 seconds). Gives BSAU time to boot + start transmitting.
 * Override in project preprocessor if needed.
 * =========================================================================================================*/
#ifndef NRF_TEST_RX_TIMEOUT_MS
  #define NRF_TEST_RX_TIMEOUT_MS  5000
#endif
/*==========================================================================================================*/

/*============== API =======================================================================================*/

/*
 * @brief   Run all NRF self-tests applicable to current role
 * @param   hnrf        Initialized NRF Handle (NRF_Init must have returned NRF_OK)
 * @param   addr        The 5-byte address passed to NRF_Init (for verification)
 * @return  TEST_PASS   if all tests pass
 *          TEST_FAIL   if any test fails
 */
TestResult NRF_Test_All(NRF_Handle *hnrf, const uint8_t addr[NRF_ADDR_WIDTH]);

/*-------------- INDIVIDUAL TESTS --------------------------------------------------------------------------*/

TestResult NRF_Test_SPI(NRF_Handle *hnrf);
TestResult NRF_Test_Registers(NRF_Handle *hnrf);
TestResult NRF_Test_Address(NRF_Handle *hnrf, const uint8_t addr[NRF_ADDR_WIDTH]);
TestResult NRF_Test_FIFO(NRF_Handle *hnrf);
TestResult NRF_Test_PowerCycle(NRF_Handle *hnrf);

#if defined(NRF_ROLE_TX)
TestResult NRF_Test_TX(NRF_Handle *hnrf);
#endif

#if defined(NRF_ROLE_RX)
TestResult NRF_Test_RX(NRF_Handle *hnrf);
#endif
/*==========================================================================================================*/

#ifdef __cplusplus
}
#endif

#endif /* NRF24L01_TEST_H */


