/*
 * @file    bsau_app.c
 * @brief   BSAU application module — implementation.
 * @author  bugrASl
 *
 * -- Packet assembly stride — critical ------------------------------------
 *
 * The DMA buffer contains 7 values per scan (6 EMG + 1 battery), so the
 * stride between scan sets is ADC_DMA_CHANNELS (= 7), NOT WL_NUM_CHANNELS
 * (= 6).
 *
 * Correct indexing:
 * g_adc_snapshot[s * ADC_DMA_CHANNELS + c]   EMG channel c of scan s
 * g_adc_snapshot[s * ADC_DMA_CHANNELS + ADC_BATT_INDEX]   battery of scan s
 */

#include "bsau_app.h"
#include "bsau_adc.h"
#include "log.h"
#if defined(BSAU_MODE_TEST_ADC_CSV) || defined(BSAU_MODE_TEST_PKT_LOG) || \
    defined(BSAU_MODE_TEST_CSV)     || defined(BSAU_MODE_TEST_DFT_LOG) || \
    defined(BSAU_MODE_TEST_NRF_LOG)
#include "bsau_test.h"
#endif
#include "nrf24l01.h"
#include "wireless_packet.h"
#include "spi.h"
#include "usart.h"
#include "main.h"
#include <string.h>

/*============== MODULE-PRIVATE STATE ======================================================================*/

static NRF_Handle       g_hnrf;
static uint8_t          tx_seq                  =   0;
static uint32_t         pkt_count               =   0;
static uint32_t         lost_count              =   0;

#define LOG_STATS_INTERVAL      329U

/*
 * LOG_CSV_DEBUG_INTERVAL: In DEBUG mode (both LOG and LOG_CSV active), emit
 * a CSV sample line every N packets for live SerialPlot capture alongside
 * structured LOG output. At ~667 pkt/s, 67 ≈ 10 Hz CSV rate.
 * In RELEASE and _LOG test modes, LOG_CSV compiles to (void)0 — zero cost.
 */
#define LOG_CSV_DEBUG_INTERVAL  67U

/*
 * NRF POR (Power-On Reset) timing.
 * Datasheet §6.1.7: up to 100 ms from VDD ramp. The 5 ms delay inside
 * NRF_Init() is for CE→standby, NOT POR. On fast-booting MCUs (L432KC
 * at 80 MHz), BSAU_Init may reach NRF_Init before the NRF chip is ready.
 */
#define NRF_POR_DELAY_MS        200U
#define NRF_INIT_RETRIES        2U
#define NRF_RETRY_BACKOFF_MS    100U
/*==========================================================================================================*/

/*============== BSAU_Init =================================================================================*/

void BSAU_Init(void)
{
    /* TEST MODE: delegate to test harness */
#if defined(BSAU_MODE_TEST_ADC_CSV) || defined(BSAU_MODE_TEST_PKT_LOG) || \
    defined(BSAU_MODE_TEST_CSV)     || defined(BSAU_MODE_TEST_DFT_LOG) || \
    defined(BSAU_MODE_TEST_NRF_LOG)
    BSAU_Test_Init();
    return;
#endif

    LOG("APP", "BSAU_Init", "RUN", "");

    assert_param(HAL_NVIC_GetPriorityGrouping() == NVIC_PRIORITYGROUP_4);
    LOG("APP", "BSAU_Init", "OK", "NVIC priority group verified");

    static const uint8_t nrf_addr[NRF_ADDR_WIDTH] = NRF_ADDRESS;

    /* NRF POR delay — wait for chip to finish internal reset after VDD ramp */
    HAL_Delay(NRF_POR_DELAY_MS);
    LOG("NRF", "NRF_Init", "RUN", "ch=%u (POR wait %ums done)", NRF_CHANNEL, NRF_POR_DELAY_MS);

    /* Init with retry — handles marginal POR timing */
    NRF_Status nrf_ret                          =   NRF_ERR_NOT_DETECTED;

    for (uint8_t attempt = 0; attempt < NRF_INIT_RETRIES; attempt++)
    {
        nrf_ret                                 =   NRF_Init(&g_hnrf, &hspi1, NRF_CHANNEL, nrf_addr);

        if (nrf_ret == NRF_OK)
        {
            break;
        }

        LOG("NRF", "NRF_Init", "WARN", "Attempt %u failed (err=%d), retrying in %ums...",
            attempt + 1, nrf_ret, NRF_RETRY_BACKOFF_MS);
        HAL_Delay(NRF_RETRY_BACKOFF_MS);
    }

    if (nrf_ret != NRF_OK)
    {
        LOG("NRF", "NRF_Init", "FAIL", "All %u attempts failed", NRF_INIT_RETRIES);
        Error_Handler();
    }

    LOG("NRF", "NRF_Init", "OK", "");

    BSAU_ADC_Init();

    LOG("APP", "BSAU_Init", "OK", "Pipeline live");
}
/*==========================================================================================================*/

/*============== BSAU_Run ================================================================================*/
 
void BSAU_Run(void)
{
    /* TEST MODE: delegate to test harness — never fall through */
#if defined(BSAU_MODE_TEST_ADC_CSV) || defined(BSAU_MODE_TEST_PKT_LOG) || \
    defined(BSAU_MODE_TEST_CSV)     || defined(BSAU_MODE_TEST_DFT_LOG) || \
    defined(BSAU_MODE_TEST_NRF_LOG)
    BSAU_Test_Run();
    return;
#endif
 
    /* RELEASE / DEBUG: normal acquisition and transmit loop */
    WL_Packet   pkt;
    uint8_t     raw[WL_PAYLOAD_SIZE];
 
    LOG("APP", "BSAU_Run", "RUN", "Entering main loop");
 
    while (1)
    {
        __WFI();
 
        if (!g_pkt_ready)
        {
            continue;
        }
        
        g_pkt_ready                             =   0;
 
        /* Check deferred ADC error from ISR */
        if (g_adc_error_code != 0)
        {
            LOG("ADC", "DeferredErr", "FAIL", "err=0x%08X", (unsigned int)g_adc_error_code);
            g_adc_error_code                    =   0;
        }
 
        /* Build packet */
        pkt.seq                                 =   tx_seq++;
        pkt.flags                               =   0;
 
        uint16_t vbat                           =   BSAU_ADC_GetBattery();
 
        if (vbat < BATT_LOW_THRESHOLD)
        {
            pkt.flags                          |=   WL_FLAG_BATT_LOW;
        }
 
        for (int s = 0; s < WL_SAMPLES_PER_PACKET; s++)
        {
            for (int c = 0; c < WL_NUM_CHANNELS; c++)
            {
                pkt.samples[s].ch[c]            =   g_adc_snapshot[s * ADC_DMA_CHANNELS + c];
            }
        }
 
        memset(pkt.reserved, 0, WL_RESERVED_SIZE);
 
        /* Encode and transmit */
        WL_Pack(&pkt, raw);
        NRF_Status status                       =   NRF_Transmit(&g_hnrf, raw);
 
        if (status == NRF_ERR_TX_MAX_RT)
        {
            NRF_FlushTX(&g_hnrf);
            lost_count++;
        }
 
        pkt_count++;
 
        /* Periodic structured status log (LOG channel) */
        if (pkt_count % LOG_STATS_INTERVAL == 0)
        {
            LOG("APP", "BSAU_Run", "INFO",
                "seq=%u batt=%u flag=%s ok=%lu lost=%lu drop=%lu",
                pkt.seq,
                vbat,
                (pkt.flags & WL_FLAG_BATT_LOW) ? "LOW" : "OK",
                pkt_count - lost_count,
                lost_count,
                (unsigned long)g_adc_dropped);
        }
 
        /*
         * DEBUG mode CSV: emit a decimated sample line so SerialPlot can
         * capture live waveforms during development. Scan 0 channels 0-5
         * + battery. LOG_CSV compiles to (void)0 in RELEASE and _LOG modes.
         */
        if (pkt_count % LOG_CSV_DEBUG_INTERVAL == 0)
        {
            LOG_CSV("%lu,%u,%u,%u,%u,%u,%u,%u",
                    (unsigned long)pkt_count,
                    pkt.samples[0].ch[0],
                    pkt.samples[0].ch[1],
                    pkt.samples[0].ch[2],
                    pkt.samples[0].ch[3],
                    pkt.samples[0].ch[4],
                    pkt.samples[0].ch[5],
                    vbat);
        }
    }
}
/*==========================================================================================================*/
