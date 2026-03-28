/*
 * @file    cpcu_test.c
 * @brief   On-target test harness implementation
 * @author  bugrASl
 *
 * Rev 6.0:
 *   - All output via LOG() and LOG_CSV() from log.h
 *   - CM4 TestMain runs NRF_Test_All() then hybrid real/synth loop
 *   - Hybrid loop: try real NRF packets first, fall back to synth
 *   - LOG_CSV used ONLY in Monitor (batch), LOG used ONLY elsewhere
 *   - Loosened timeouts throughout
 */

#include "cpcu_test.h"
#include "cpcu_ipc.h"
#include "wireless_packet.h"
#include "log.h"
#include "iwdg.h"
#include "main.h"
#include <string.h>
#include <stdio.h>
#include <stdarg.h>

/* NRF test suite + CM4 app externs — CM4 only */
#if defined(CORE_CM4)
#include "cpcu_cm4_app.h"
#include "nrf24l01_test.h"
#include "spi.h"
#endif

/*============== CORE-SPECIFIC IWDG ====================================================================*/
#if defined(CORE_CM7)
  #define CPCU_TEST_IWDG  hiwdg1
#elif defined(CORE_CM4)
  #define CPCU_TEST_IWDG  hiwdg2
#else
  #error "Neither CORE_CM7 nor CORE_CM4 defined."
#endif
/*======================================================================================================*/

/*============== PRIVATE: Test assertion ===============================================================*/
static bool test_assert(bool condition, const char *test_name, const char *fmt, ...)
{
    char detail[128];
    va_list args;
    va_start(args, fmt);
    vsnprintf(detail, sizeof(detail), fmt, args);
    va_end(args);

    if (condition)
        LOG("TEST", test_name, "PASS", "%s", detail);
    else
        LOG("TEST", test_name, "FAIL", "%s", detail);
    return condition;
}
/*======================================================================================================*/

/*============== TEST MODULE 1: Wireless Packet Codec (CM7 only) =======================================*/
#if defined(CORE_CM7)

TestResult CPCU_Test_WirelessPacket(void)
{
    LOG("TEST", "WirelessPacket", "RUN", "starting codec tests");

    bool all_pass = true;
    WL_Packet tx = {0}, rx = {0};
    uint8_t raw[WL_PAYLOAD_SIZE];

    /* 1.1: Roundtrip with known pattern */
    tx.seq   = 42;
    tx.flags = WL_BATT_SET(0, WL_BATT_LOW);
    for (int s = 0; s < WL_SAMPLES_PER_PACKET; s++)
        for (int c = 0; c < WL_NUM_CHANNELS; c++)
            tx.samples[s].ch[c] = (uint16_t)((s * 100 + c * 10) & 0xFFF);

    WL_Pack(&tx, raw);
    WL_Unpack(raw, &rx);

    all_pass &= test_assert(rx.seq == 42, "WL_Roundtrip", "seq=42");
    all_pass &= test_assert(WL_BATT_GET(rx.flags) == WL_BATT_LOW, "WL_Roundtrip", "flags=BATT_LOW");

    bool samples_ok = true;
    for (int s = 0; s < WL_SAMPLES_PER_PACKET && samples_ok; s++)
        for (int c = 0; c < WL_NUM_CHANNELS && samples_ok; c++)
            if (rx.samples[s].ch[c] != tx.samples[s].ch[c]) samples_ok = false;
    all_pass &= test_assert(samples_ok, "WL_Roundtrip", "all sample data matches");

    /* 1.2: Extreme values */
    memset(&tx, 0, sizeof(tx));
    for (int s = 0; s < WL_SAMPLES_PER_PACKET; s++)
        for (int c = 0; c < WL_NUM_CHANNELS; c++)
            tx.samples[s].ch[c] = 4095;
    WL_Pack(&tx, raw);
    WL_Unpack(raw, &rx);

    bool max_ok = true;
    for (int s = 0; s < WL_SAMPLES_PER_PACKET && max_ok; s++)
        for (int c = 0; c < WL_NUM_CHANNELS && max_ok; c++)
            if (rx.samples[s].ch[c] != 4095) max_ok = false;
    all_pass &= test_assert(max_ok, "WL_Extremes", "all channels = 4095");

    memset(&tx, 0, sizeof(tx));
    WL_Pack(&tx, raw);
    WL_Unpack(raw, &rx);

    bool zero_ok = true;
    for (int s = 0; s < WL_SAMPLES_PER_PACKET && zero_ok; s++)
        for (int c = 0; c < WL_NUM_CHANNELS && zero_ok; c++)
            if (rx.samples[s].ch[c] != 0) zero_ok = false;
    all_pass &= test_assert(zero_ok, "WL_Extremes", "all channels = 0");

    /* 1.3: Alternating bit pattern */
    memset(&tx, 0, sizeof(tx));
    for (int s = 0; s < WL_SAMPLES_PER_PACKET; s++)
        for (int c = 0; c < WL_NUM_CHANNELS; c++)
            tx.samples[s].ch[c] = (c % 2 == 0) ? 0xAAA : 0x555;
    WL_Pack(&tx, raw);
    WL_Unpack(raw, &rx);

    bool alt_ok = true;
    for (int s = 0; s < WL_SAMPLES_PER_PACKET && alt_ok; s++)
        for (int c = 0; c < WL_NUM_CHANNELS && alt_ok; c++)
            if (rx.samples[s].ch[c] != ((c % 2 == 0) ? 0xAAA : 0x555)) alt_ok = false;
    all_pass &= test_assert(alt_ok, "WL_Alternating", "0xAAA/0x555 pattern intact");

    /* 1.4: All flags bits roundtrip */
    memset(&tx, 0, sizeof(tx));
    tx.flags = 0xFF;
    WL_Pack(&tx, raw);
    WL_Unpack(raw, &rx);
    all_pass &= test_assert(rx.flags == 0xFF, "WL_FlagsAll", "flags=0x%02X (expect 0xFF)", rx.flags);

    LOG("TEST", "WirelessPacket", all_pass ? "PASS" : "FAIL",
        all_pass ? "all codec tests passed" : "one or more codec tests failed");
    return all_pass ? TEST_PASS : TEST_FAIL;
}

#endif /* CORE_CM7 */
/*======================================================================================================*/

/*============== TEST MODULE 2: IPC Ring Buffer ========================================================*/
TestResult CPCU_Test_IPC(void)
{
    LOG("TEST", "IPC", "RUN", "starting IPC tests");

    bool all_pass = true;
    volatile IPC_ControlBlock *ctrl = IPC_CTRL();
    volatile IPC_Diagnostics  *diag = IPC_DIAG();

    /* 2.1: Control block integrity */
    all_pass &= test_assert(ctrl->magic == IPC_MAGIC, "IPC_Magic", "magic=0x%08lX", (unsigned long)ctrl->magic);
    all_pass &= test_assert(ctrl->version == IPC_VERSION, "IPC_Version", "version=0x%04X", ctrl->version);
    all_pass &= test_assert(ctrl->m7_ready == 1, "IPC_M7Ready", "m7_ready=%u", ctrl->m7_ready);

    /* 2.2: Single push/pop */
    WL_Packet test_pkt = {0};
    test_pkt.seq = 99;
    test_pkt.samples[0].ch[0] = 1234;
    test_pkt.samples[0].ch[1] = 5678;
    IPC_M4_PushSensor(&test_pkt);

    IPC_SensorEntry entry;
    bool popped = IPC_M7_PopSensor(&entry);
    all_pass &= test_assert(popped, "IPC_PushPop", "pop returned true");
    all_pass &= test_assert(entry.seq == 99, "IPC_PushPop", "seq=%u (expected 99)", entry.seq);
    all_pass &= test_assert(entry.samples[0].ch[0] == 1234, "IPC_PushPop", "ch0=%u (expected 1234)", entry.samples[0].ch[0]);
    all_pass &= test_assert(entry.samples[0].ch[1] == 5678, "IPC_PushPop", "ch1=%u (expected 5678)", entry.samples[0].ch[1]);

    /* 2.3: Overflow */
    uint32_t ovf_before = diag->m4_ring_overflows;
    for (int i = 0; i < TEST_IPC_OVERFLOW_COUNT; i++)
    {
        WL_Packet pkt = {0};
        pkt.seq = (uint8_t)(i & 0xFF);
        pkt.samples[0].ch[0] = (uint16_t)(i & 0xFFF);
        IPC_M4_PushSensor(&pkt);
    }
    uint32_t ring_count = IPC_SensorCount();
    uint32_t ovf_after  = diag->m4_ring_overflows;
    all_pass &= test_assert(ring_count <= IPC_SENSOR_RING_SIZE, "IPC_Overflow", "count=%lu (max=%u)", (unsigned long)ring_count, IPC_SENSOR_RING_SIZE);
    all_pass &= test_assert(ovf_after > ovf_before, "IPC_Overflow", "ovf before=%lu after=%lu", (unsigned long)ovf_before, (unsigned long)ovf_after);

    IPC_SensorEntry first;
    IPC_M7_PopSensor(&first);
    all_pass &= test_assert(first.seq == 2, "IPC_Overflow", "first_seq=%u (expected 2)", first.seq);

    /* Drain remaining */
    IPC_SensorEntry drain;
    while (IPC_M7_PopSensor(&drain)) {}

    /* 2.4: Batch read */
    for (int i = 0; i < 10; i++)
    {
        WL_Packet pkt = {0};
        pkt.seq = (uint8_t)i;
        IPC_M4_PushSensor(&pkt);
    }
    IPC_SensorEntry batch[IPC_SENSOR_RING_SIZE];
    uint32_t batch_count = 0;
    IPC_M7_PopSensorBatch(batch, &batch_count);
    all_pass &= test_assert(batch_count == 10, "IPC_Batch", "popped=%lu (expected 10)", (unsigned long)batch_count);
    all_pass &= test_assert(batch[0].seq == 0 && batch[9].seq == 9, "IPC_Batch", "first=%u last=%u", batch[0].seq, batch[9].seq);

    /* 2.5: Motor command roundtrip */
    IPC_MotorCommand tx_cmd = {0};
    tx_cmd.servo_us[0] = 1500;
    tx_cmd.servo_us[1] = 1200;
    tx_cmd.gesture_id  = 3;
    tx_cmd.confidence  = 95;
    IPC_M7_PushMotorCommand(&tx_cmd);

    IPC_MotorCommand rx_cmd;
    bool cmd_ok = IPC_M4_ReadMotorCommand(&rx_cmd);
    all_pass &= test_assert(cmd_ok, "IPC_MotorCmd", "read returned true");
    all_pass &= test_assert(rx_cmd.servo_us[0] == 1500 && rx_cmd.servo_us[1] == 1200, "IPC_MotorCmd", "s0=%u s1=%u", rx_cmd.servo_us[0], rx_cmd.servo_us[1]);
    all_pass &= test_assert(rx_cmd.gesture_id == 3 && rx_cmd.confidence == 95, "IPC_MotorCmd", "g=%u c=%u", rx_cmd.gesture_id, rx_cmd.confidence);

    /* 2.6: Empty ring pop returns false */
    IPC_SensorEntry empty_entry;
    bool empty_pop = IPC_M7_PopSensor(&empty_entry);
    all_pass &= test_assert(!empty_pop, "IPC_EmptyPop", "pop on empty ring returned false");

    LOG("TEST", "IPC", all_pass ? "PASS" : "FAIL",
        all_pass ? "all IPC tests passed" : "one or more IPC tests failed");
    return all_pass ? TEST_PASS : TEST_FAIL;
}
/*======================================================================================================*/

/*============== TEST MODULE 3: Live Monitor ===========================================================*/
void CPCU_Test_Monitor(void)
{
    LOG("TEST", "Monitor", "RUN", "entering CSV monitor (%u lines)", TEST_MONITOR_CSV_LINES);

    LOG_CSV("# timestamp,seq,ch0,ch1,ch2,ch3,ch4,ch5,ring_count,overflows,gaps,state");

    uint32_t last_diag_tick = HAL_GetTick();
    uint32_t csv_lines      = 0;

    while (csv_lines < TEST_MONITOR_CSV_LINES)
    {
        IPC_SensorEntry entry;
        while (IPC_M7_PopSensor(&entry) && csv_lines < TEST_MONITOR_CSV_LINES)
        {
            LOG_CSV("%lu,%u,%u,%u,%u,%u,%u,%u,%lu,%lu,%lu,%u",
                (unsigned long)entry.timestamp, entry.seq,
                entry.samples[0].ch[0], entry.samples[0].ch[1],
                entry.samples[0].ch[2], entry.samples[0].ch[3],
                entry.samples[0].ch[4], entry.samples[0].ch[5],
                (unsigned long)IPC_SensorCount(),
                (unsigned long)IPC_DIAG()->m4_ring_overflows,
                (unsigned long)IPC_DIAG()->m4_seq_gaps,
                IPC_CTRL()->system_state);
            csv_lines++;
        }

        if ((HAL_GetTick() - last_diag_tick) >= TEST_MONITOR_DIAG_MS)
        {
            last_diag_tick = HAL_GetTick();
            LOG_CSV("# [%lus] pkts=%lu drops=%lu ovf=%lu gaps=%lu batches=%lu state=%u",
                (unsigned long)(HAL_GetTick() / 1000),
                (unsigned long)IPC_DIAG()->m4_pkts_received,
                (unsigned long)IPC_DIAG()->m4_pkts_dropped,
                (unsigned long)IPC_DIAG()->m4_ring_overflows,
                (unsigned long)IPC_DIAG()->m4_seq_gaps,
                (unsigned long)IPC_DIAG()->m7_batches_processed,
                IPC_CTRL()->system_state);
        }

        HAL_IWDG_Refresh(&CPCU_TEST_IWDG);
    }

    LOG("TEST", "Monitor", "OK", "CSV capture complete: %lu lines", (unsigned long)csv_lines);

    LOG("TEST", "Monitor", "INFO", "final: pkts=%lu drops=%lu ovf=%lu gaps=%lu nrf=%lu",
        (unsigned long)IPC_DIAG()->m4_pkts_received,
        (unsigned long)IPC_DIAG()->m4_pkts_dropped,
        (unsigned long)IPC_DIAG()->m4_ring_overflows,
        (unsigned long)IPC_DIAG()->m4_seq_gaps,
        (unsigned long)IPC_DIAG()->m4_nrf_init_status);

    IPC_CTRL()->system_state = IPC_STATE_INIT;
    __DMB();

    while (1)
    {
        IPC_SensorEntry discard;
        while (IPC_M7_PopSensor(&discard)) {}
        HAL_IWDG_Refresh(&CPCU_TEST_IWDG);
        HAL_Delay(100);
    }
}
/*======================================================================================================*/

/*============== CM7 TEST MAIN =========================================================================*/
void CPCU_CM7_TestMain(void)
{
    LOG("TEST", "CM7_TestMain", "RUN", "================ CPCU TEST SUITE START =================");

    uint32_t pass = 0, fail = 0, skip = 0;
    TestResult r;

    /* Unit tests */
    r = CPCU_Test_WirelessPacket();
    if (r == TEST_PASS) pass++; else if (r == TEST_FAIL) fail++; else skip++;

    /* Wait for CM4 */
    uint32_t wait_start = HAL_GetTick();
    while (!IPC_CTRL()->m4_ready && (HAL_GetTick() - wait_start) < 3000)
        HAL_IWDG_Refresh(&CPCU_TEST_IWDG);

    /* Isolate IPC for unit tests */
    volatile IPC_ControlBlock *ctrl = IPC_CTRL();
    volatile IPC_Diagnostics  *diag = IPC_DIAG();

    ctrl->system_state = IPC_STATE_INIT;
    __DMB();

    /* Double drain to clear any CM4 synthetic data */
    { IPC_SensorEntry db[IPC_SENSOR_RING_SIZE]; uint32_t dc; IPC_M7_PopSensorBatch(db, &dc); }
    HAL_Delay(100);
    { IPC_SensorEntry db[IPC_SENSOR_RING_SIZE]; uint32_t dc; IPC_M7_PopSensorBatch(db, &dc); }

    ctrl->sensor_head = 0;
    ctrl->sensor_tail = 0;
    diag->m4_ring_overflows = 0;
    diag->m4_pkts_received  = 0;
    __DMB();

    if (IPC_CTRL()->m4_ready)
    {
        r = CPCU_Test_IPC();
        if (r == TEST_PASS) pass++; else if (r == TEST_FAIL) fail++; else skip++;
    } else
    {
        LOG("TEST", "IPC", "SKIP", "CM4 not ready after 3s");
        skip++;
    }

    ctrl->system_state = IPC_STATE_RUNNING;
    __DMB();

    /* Report NRF init status from CM4 */
    LOG("TEST", "CM7_TestMain", "INFO", "CM4 NRF init status=%lu", (unsigned long)diag->m4_nrf_init_status);

    LOG("TEST", "CM7_TestMain", "INFO",
        "=== RESULTS: %lu PASS, %lu FAIL, %lu SKIP ===",
        (unsigned long)pass, (unsigned long)fail, (unsigned long)skip);

    LOG("TEST", "CM7_TestMain", "INFO", "entering live monitor...");
    CPCU_Test_Monitor();
}
/*======================================================================================================*/

/*============== CM4 TEST MAIN (Rev 6.0: NRF tests + hybrid loop) ======================================*/
/*
 * Enhanced CM4 test mode:
 *   1. Run NRF_Test_All() if NRF init succeeded
 *   2. Enter hybrid loop: check for real NRF packets, fall back to synth
 *   3. Pause synthetic when CM7 sets state=INIT (IPC isolation)
 *
 * The hybrid approach lets you test with or without BSAU connected.
 * Real packets take priority; synthetic only kicks in after
 * TEST_REAL_PKT_WINDOW_MS with no real data.
 */
void CPCU_CM4_TestMain(void)
{
    LOG("TEST", "CM4_TestMain", "RUN", "CM4 test mode active");

#if defined(CORE_CM4)
    /*
     * NRF self-test suite — only if NRF init succeeded.
     * g_hnrf, g_nrf_init_ok declared in cpcu_cm4_app.h.
     */

    if (g_nrf_init_ok)
    {
        static const uint8_t test_addr[NRF_ADDR_WIDTH] = NRF_RX_ADDRESS;
        LOG("TEST", "CM4_TestMain", "RUN", "running NRF self-test suite...");

        TestResult nrf_result = NRF_Test_All(&g_hnrf, test_addr);

        LOG("TEST", "CM4_TestMain", nrf_result == TEST_PASS ? "PASS" : "FAIL",
            "NRF self-test %s", nrf_result == TEST_PASS ? "passed" : "FAILED");
    } else
    {
        LOG("TEST", "CM4_TestMain", "SKIP", "NRF tests skipped — init failed (nrf_init_ok=false)");
    }
#endif

    LOG("TEST", "CM4_TestMain", "RUN", "entering hybrid loop (real=%ums synth=%ums)",
        TEST_REAL_PKT_WINDOW_MS, TEST_SYNTH_INTERVAL_MS);

    uint32_t synth_tick     = HAL_GetTick();
    uint32_t last_real_tick = HAL_GetTick();
    uint8_t  synth_seq      = 0;
    bool     got_real_pkt   = false;

    while (1)
    {
        volatile IPC_ControlBlock *ctrl = IPC_CTRL();

        /* Pause during IPC unit tests */
        if (ctrl->system_state == IPC_STATE_INIT)
        {
            HAL_IWDG_Refresh(&CPCU_TEST_IWDG);
            HAL_Delay(10);
            continue;
        }

#if defined(CORE_CM4)
        /*
         * Check for real NRF packets.
         * We poll NRF status directly rather than relying on nrf_irq_flag,
         * because the EXTI callback is in cpcu_cm4_app.c's normal path
         * and we're in the test path. Direct polling works for testing.
         */
        if (g_nrf_init_ok)
        {
            /* nrf_irq_flag declared in cpcu_cm4_app.h */
            if (nrf_irq_flag || NRF_DataAvailable(&g_hnrf))
            {
                nrf_irq_flag = false;
                while (NRF_DataAvailable(&g_hnrf))
                {
                    uint8_t buf[NRF_PAYLOAD_SIZE];
                    if (NRF_ReadPayload(&g_hnrf, buf) == NRF_OK)
                    {
                        WL_Packet pkt;
                        WL_Unpack(buf, &pkt);
                        IPC_M4_PushSensor(&pkt);
                        last_real_tick = HAL_GetTick();
                        got_real_pkt   = true;

                        LOG("TEST", "CM4_RealPkt", "OK", "seq=%u ch0=%u flags=0x%02X",
                            pkt.seq, pkt.samples[0].ch[0], pkt.flags);
                    }
                }
                NRF_ClearIRQ(&g_hnrf, NRF_STATUS_RX_DR);
            }
        }
#endif

        /*
         * Synthetic fallback: generate fake data if no real packet
         * has arrived within the window. This keeps the CM7 monitor fed.
         */
        bool real_timeout = (HAL_GetTick() - last_real_tick) > TEST_REAL_PKT_WINDOW_MS;

        if (real_timeout && (HAL_GetTick() - synth_tick) >= TEST_SYNTH_INTERVAL_MS)
        {
            synth_tick = HAL_GetTick();

            WL_Packet synth = {0};
            synth.seq       = synth_seq++;
            synth.flags     = 0;
            for (int s = 0; s < WL_SAMPLES_PER_PACKET; s++)
                for (int c = 0; c < WL_NUM_CHANNELS; c++)
                    synth.samples[s].ch[c] = (uint16_t)(((uint16_t)synth_seq * 16 + c * 100 + s * 50) & 0xFFF);

            IPC_M4_PushSensor(&synth);

            /* Log first synth packet to confirm fallback activated */
            if (synth_seq == 1 && !got_real_pkt)
            {
                LOG("TEST", "CM4_Synth", "INFO", "no real pkts for %ums — synthetic mode", TEST_REAL_PKT_WINDOW_MS);
            }
        }

        HAL_IWDG_Refresh(&CPCU_TEST_IWDG);
    }
}
/*======================================================================================================*/
