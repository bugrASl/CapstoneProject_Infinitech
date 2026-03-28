/*
 * @file    cpcu_cm7_app.c
 * @brief   CPCU CM7 Cognitive Layer — implementation.
 * @author  bugrASl
 *
 * ── Threshold Classifier (DSP/ML placeholder) ────────────────────
 *
 *   Until the full DSP+ML pipeline is implemented, a simple threshold
 *   classifier maps raw EMG levels to servo positions.
 *
/* Rev 6.0: all debug output via LOG() from log.h */

#include "cpcu_cm7_app.h"
#include "cpcu_ipc.h"
#include "log.h"
#include "usart.h"
#include "iwdg.h"
#include "main.h"
#include <stdio.h>
#include <string.h>

#if defined(CPCU_MODE_TEST_LOG) || defined(CPCU_MODE_TEST_CSV)
#include "cpcu_test.h"
#endif

/* All configuration defines (THR_LOW/MID/HIGH, CM7_DEBUG_INTERVAL_MS) are in cpcu_cm7_app.h */

/*============== MODULE-PRIVATE STATE ==================================================================*/
                                                
static IPC_SensorEntry  g_entries[IPC_SENSOR_RING_SIZE];
static uint32_t         g_last_debug_tick       =   0;
/*======================================================================================================*/

/*============== PRINTF REDIRECT =======================================================================*/

int __io_putchar(int ch)
{
    HAL_UART_Transmit(&huart3, (uint8_t *)&ch, 1, 10);
    return ch;
}
/*======================================================================================================*/

/*============== PRIVATE HELPERS =======================================================================*/

static void DWT_Init(void)
{
    CoreDebug->DEMCR                           |=   CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT                                 =   0;
    DWT->CTRL                                  |=   DWT_CTRL_CYCCNTENA_Msk;
    LOG("CM7", "DWT_Init", "OK", "Cycle counter enabled");
}

static void PrintDiagnostics(void)
{
    volatile IPC_Diagnostics  *d                =   IPC_DIAG();
    volatile IPC_ControlBlock *c                =   IPC_CTRL();

    LOG("CM7", "Diagnostics", "INFO",
        "t=%lus state=%u pkts=%lu gaps=%lu ovf=%lu batches=%lu m4=%u nrf=%lu",
        (unsigned long)(HAL_GetTick() / 1000),
        c->system_state,
        (unsigned long)d->m4_pkts_received,
        (unsigned long)d->m4_seq_gaps,
        (unsigned long)d->m4_ring_overflows,
        (unsigned long)d->m7_batches_processed,
        c->m4_ready,
        (unsigned long)d->m4_nrf_init_status);

    /* Report NRF health if it failed at boot */
    if (d->m4_nrf_init_status != 0)
    {
        LOG("CM7", "Diagnostics", "WARN",
            "CM4 NRF init status=%lu (0=OK, 2=NOT_DETECTED) safe_entries=%lu wdg=%lu",
            (unsigned long)d->m4_nrf_init_status,
            (unsigned long)d->m4_safe_state_entries,
            (unsigned long)d->m4_watchdog_events);
    }
}

/* Threshold Classifier */

static uint8_t ClassifyChannel(uint16_t avg)
{
    if (avg < THR_LOW) 
    {
        return 0;
    }
    if (avg < THR_MID)  
    {
        return 1;
    }
    if (avg < THR_HIGH) 
    {
        return 2;
    }
    return 3;
}

static uint16_t ZoneToServo(uint8_t zone)
{
    uint16_t travel                             =   SERVO_ABS_MAX_PULSE - SERVO_NEUTRAL;
    uint16_t step                               =   travel / 3;
    return SERVO_NEUTRAL + (uint16_t)zone * step;
}

static void ThresholdClassify(const IPC_SensorEntry *entry, IPC_MotorCommand *cmd, uint16_t avg_out[WL_NUM_CHANNELS])
{
    uint8_t max_zone                            =   0;

    for (int ch = 0; ch < WL_NUM_CHANNELS; ch++)
    {
        uint32_t sum                            =   0;
        for (int s = 0; s < WL_SAMPLES_PER_PACKET; s++)
        {
            sum                                +=   entry->samples[s].ch[ch];
        }
        uint16_t avg                            =   (uint16_t)(sum / WL_SAMPLES_PER_PACKET);
        avg_out[ch]                             =   avg;

        uint8_t zone                            =   ClassifyChannel(avg);
        cmd->servo_us[ch]                       =   ZoneToServo(zone);

        if (zone > max_zone) 
        {
            max_zone = zone;
        }
    }

    cmd->gesture_id                             =   max_zone;
    cmd->confidence                             =   (max_zone == 0) ? 
                                                                0   :   (uint8_t)(max_zone * 33);
    cmd->timestamp                              =   HAL_GetTick();
}
/*======================================================================================================*/

/*============== CPCU_CM7_Init =========================================================================*/

void CPCU_CM7_Init(void)
{
    printf("\r\n\r\n");

    DWT_Init();
                                                                                
    LOG("CM7", "CPCU_CM7_Init", "RUN", "=================================================");
    LOG("CM7", "CPCU_CM7_Init", "INFO", "InfiniTech CPCU — CM7 Online");
    LOG("CM7", "CPCU_CM7_Init", "INFO", "SYSCLK=%lu MHz  HCLK=%lu MHz",
        (unsigned long)(HAL_RCC_GetSysClockFreq() / 1000000),
        (unsigned long)(HAL_RCC_GetHCLKFreq() / 1000000));

#if defined(CPCU_MODE_TEST_LOG) || defined(CPCU_MODE_TEST_CSV)
    LOG("CM7", "CPCU_CM7_Init", "INFO", "BUILD   =   TEST");
#elif defined(CPCU_MODE_DEBUG)
    LOG("CM7", "CPCU_CM7_Init", "INFO", "BUILD   =   DEBUG");
#else
    LOG("CM7", "CPCU_CM7_Init", "INFO", "BUILD   =   RELEASE");
#endif

    LOG("CM7", "CPCU_CM7_Init", "INFO",
        "Threshold low=%u mid=%u high=%u", 
        THR_LOW, THR_MID, THR_HIGH);

    volatile IPC_ControlBlock *ctrl             =   IPC_CTRL();
    LOG("CM7", "CPCU_CM7_Init", "OK",
        "IPC magic=0x%08lX ver=0x%04X m7=%u m4=%u",
        (unsigned long)ctrl->magic, ctrl->version,
        ctrl->m7_ready, ctrl->m4_ready);
                                                                                   
    LOG("CM7", "CPCU_CM7_Init", "OK", "=================================================");

    g_last_debug_tick                           =   HAL_GetTick();
}
/*======================================================================================================*/

/*============== CPCU_CM7_Run ==========================================================================*/

void CPCU_CM7_Run(void)
{
#if defined(CPCU_MODE_TEST_LOG) || defined(CPCU_MODE_TEST_CSV)
    CPCU_CM7_TestMain();
#endif

    LOG("CM7", "CPCU_CM7_Run", "RUN", "Entering cognitive loop");

    IPC_MotorCommand cmd;
    uint16_t avg_channels[WL_NUM_CHANNELS];

    while (1)
    {
        /* 1. Drain sensor ring buffer */
        uint32_t count                          =   0;
        IPC_M7_PopSensorBatch(g_entries, &count);

        if (count > 0)
        {
            LOG("CM7", "PopSensorBatch", "OK", "count=%lu", (unsigned long)count);

            ThresholdClassify(&g_entries[count - 1], &cmd, avg_channels);

            LOG_CSV("%lu,%u,%u,%u,%u,%u,%u,%u,%u,%u",
                    (unsigned long)g_entries[count - 1].timestamp,
                    g_entries[count - 1].seq,
                    avg_channels[0], avg_channels[1],
                    avg_channels[2], avg_channels[3],
                    avg_channels[4], avg_channels[5],
                    cmd.gesture_id, cmd.confidence);

            IPC_M7_PushMotorCommand(&cmd);

            LOG("CM7", "MotorCmd", "OK",
                "zone=%u conf=%u%% s=[%u,%u,%u,%u,%u,%u]",
                cmd.gesture_id, cmd.confidence,
                cmd.servo_us[0], cmd.servo_us[1],
                cmd.servo_us[2], cmd.servo_us[3],
                cmd.servo_us[4], cmd.servo_us[5]);
        }

        /* 2. Periodic diagnostics */
        if ((HAL_GetTick() - g_last_debug_tick) >= CM7_DEBUG_INTERVAL_MS)
        {
            g_last_debug_tick                   =   HAL_GetTick();
            PrintDiagnostics();
        }

        /* 3. Hardware watchdog */
        HAL_IWDG_Refresh(&hiwdg1);
    }
}
/*==========================================================================================================*/
