/*
 * @file    cpcu_cm4_app.c
 * @brief   CPCU CM4 Reflex Layer — implementation.
 * @author  bugrASl
 *
 * Rev 6.0 (2026-03-28):
 *   FIX: NRF init retry with 200 ms POR delay + 3 attempts
 *   FIX: NRF init failure → DEGRADED (recoverable), not permanent SAFE
 *   FIX: NRF re-init every 3 s while in DEGRADED with init failure
 *   FIX: E-STOP double-entry guard
 *   FIX: m4_watchdog_events only incremented on actual IWDG2 recovery
 *   FIX: RADIO_TIMEOUT_MS loosened 500→750 ms, IWDG 500→1000 ms
 *   ENH: All debug output via LOG() from log.h (replaces cpcu_debug.h)
 *   ENH: Init summary line with radio status + system state
 */

#include "cpcu_cm4_app.h"
#include "cpcu_ipc.h"
#include "nrf24l01.h"
#include "wireless_packet.h"
#include "log.h"
#include "spi.h"
#include "tim.h"
#include "iwdg.h"
#include "usart.h"
#include "main.h"
#include <string.h>
#include <stdbool.h>

#if defined(CPCU_MODE_TEST_LOG) || defined(CPCU_MODE_TEST_CSV)
#include "cpcu_test.h"
#endif

/* All configuration defines, RadioState enum, and servo limits are in cpcu_cm4_app.h */

/*============== PRINTF REDIRECT =======================================================================*/
int __io_putchar(int ch)
{
    HAL_UART_Transmit(&huart3, (uint8_t *)&ch, 1, 10);
    return ch;
}

static const uint16_t servo_min[IPC_NUM_SERVOS] = SERVO_MIN_VALUES;
static const uint16_t servo_max[IPC_NUM_SERVOS] = SERVO_MAX_VALUES;
/*======================================================================================================*/

/*============== LINK QUALITY ==========================================================================*/
typedef struct {
    uint32_t window_start_tick;
    uint32_t window_pkts;
    uint32_t window_lost;
    uint32_t window_jitter_sum;
    uint32_t window_jitter_max;
    uint32_t window_rpd_sum;
    uint32_t last_pkt_tick;
    bool     first_pkt_in_window;
    uint16_t loss_pct_x10;
    uint16_t arrival_rate;
    uint16_t jitter_avg_ms_x10;
    uint16_t jitter_max_ms;
    uint8_t  rpd;
    uint8_t  link_quality;
} NRF_LinkStats;
/*======================================================================================================*/

/*============== MODULE-PRIVATE STATE ==================================================================*/

NRF_Handle              g_hnrf;               /* non-static: accessed by cpcu_test.c via extern */
volatile bool           nrf_irq_flag        = false;  /* non-static: accessed by cpcu_test.c via extern */
static volatile bool    estop_flag          = false;
static uint8_t          nrf_raw_buf[WL_PAYLOAD_SIZE];
static WL_Packet        rx_pkt;
static IPC_MotorCommand motor_cmd;

static uint32_t         last_valid_pkt_tick = 0;
static uint8_t          expected_seq        = 0;

static RadioState       radio_state         = RADIO_RUNNING;
static uint32_t         degraded_enter_tick = 0;
static uint32_t         recovery_pkt_count  = 0;
static uint32_t         min_valid_cmd_seq   = 0;

static NRF_LinkStats    g_link_stats;

bool                    g_nrf_init_ok       = false;  /* non-static: accessed by cpcu_test.c */
static uint32_t         g_last_reinit_tick  = 0;

extern bool g_iwdg2_recovery;

static const uint8_t nrf_address[NRF_ADDR_WIDTH] = NRF_RX_ADDRESS;
/*======================================================================================================*/

/*============== PRIVATE HELPERS =======================================================================*/

static void SafetyClamp(uint16_t servo_us[IPC_NUM_SERVOS])
{
    for (int i = 0; i < IPC_NUM_SERVOS; i++)
    {
        uint16_t original = servo_us[i];
        if (servo_us[i] < servo_min[i]) servo_us[i] = servo_min[i];
        if (servo_us[i] > servo_max[i]) servo_us[i] = servo_max[i];
        if (servo_us[i] != original)
        {
            LOG("PWM", "SafetyClamp", "WARN", "servo[%d] clamped %u->%u", i, original, servo_us[i]);
        }
    }
}

static void ServosToNeutral(void)
{
    __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_1, SERVO_NEUTRAL);
    __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_2, SERVO_NEUTRAL);
    __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_3, SERVO_NEUTRAL);
    __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_4, SERVO_NEUTRAL);
    __HAL_TIM_SET_COMPARE(&htim8, TIM_CHANNEL_1, SERVO_NEUTRAL);
    __HAL_TIM_SET_COMPARE(&htim8, TIM_CHANNEL_2, SERVO_NEUTRAL);
}

static void EnterSafeState(void)
{
    IPC_CTRL()->system_state = IPC_STATE_SAFE;
    radio_state              = RADIO_SAFE;
    ServosToNeutral();
    IPC_DIAG()->m4_safe_state_entries++;
    LOG("CM4", "EnterSafeState", "WARN", "PERMANENT safe state — reset required");
}

static void EnterDegradedState(void)
{
    IPC_CTRL()->system_state = IPC_STATE_SAFE;
    radio_state              = RADIO_DEGRADED;
    degraded_enter_tick      = HAL_GetTick();
    recovery_pkt_count       = 0;
    ServosToNeutral();
    LOG("CM4", "RadioDegraded", "WARN", "no pkt for %lu ms — servos neutral", (unsigned long)RADIO_TIMEOUT_MS);
}

static void EnterRunningState(void)
{
    IPC_CTRL()->system_state = IPC_STATE_RUNNING;
    radio_state              = RADIO_RUNNING;
    min_valid_cmd_seq        = IPC_CTRL()->motor_cmd_seq;
    LOG("CM4", "RadioRecovered", "OK", "link stable — %lu ok pkts, servos active", (unsigned long)RECOVERY_PKT_COUNT);
}

static inline uint32_t us_to_ccr(uint16_t pulse_us) { return (uint32_t)pulse_us; }
/*======================================================================================================*/

/*============== LINK QUALITY ==========================================================================*/

static void LinkStats_ResetWindow(NRF_LinkStats *ls)
{
    ls->window_start_tick  = HAL_GetTick();
    ls->window_pkts        = 0;
    ls->window_lost        = 0;
    ls->window_jitter_sum  = 0;
    ls->window_jitter_max  = 0;
    ls->window_rpd_sum     = 0;
    ls->first_pkt_in_window = true;
}

static void LinkStats_Init(NRF_LinkStats *ls)
{
    memset(ls, 0, sizeof(*ls));
    ls->window_start_tick   = HAL_GetTick();
    ls->first_pkt_in_window = true;
    ls->link_quality        = 100;
}

static void LinkStats_FeedPacket(NRF_LinkStats *ls, NRF_Handle *hnrf, uint8_t rx_seq, uint8_t exp_seq)
{
    uint32_t now = HAL_GetTick();
    ls->window_pkts++;
    if (rx_seq != exp_seq) ls->window_lost += (uint8_t)(rx_seq - exp_seq);

    if (!ls->first_pkt_in_window)
    {
        uint32_t delta = now - ls->last_pkt_tick;
        ls->window_jitter_sum += delta;
        if (delta > ls->window_jitter_max) ls->window_jitter_max = delta;
    }
    ls->first_pkt_in_window = false;
    ls->last_pkt_tick       = now;
    ls->rpd                 = NRF_ReadReg(hnrf, NRF_REG_RPD) & 0x01;
    ls->window_rpd_sum     += ls->rpd;
}

static void LinkStats_ComputeWindow(NRF_LinkStats *ls)
{
    uint32_t elapsed = HAL_GetTick() - ls->window_start_tick;
    if (elapsed == 0) elapsed = 1;

    uint32_t total        = ls->window_pkts + ls->window_lost;
    ls->loss_pct_x10      = (total > 0) ? (uint16_t)((ls->window_lost * 1000) / total) : 0;
    ls->arrival_rate      = (uint16_t)((ls->window_pkts * 1000) / elapsed);
    ls->jitter_avg_ms_x10 = (ls->window_pkts > 1) ? (uint16_t)((ls->window_jitter_sum * 10) / (ls->window_pkts - 1)) : 0;
    ls->jitter_max_ms     = (uint16_t)ls->window_jitter_max;

    int32_t score = 100;
    if (total > 0) score -= (int32_t)(ls->window_lost * 50) / (int32_t)total;
    uint32_t jit_ms = ls->jitter_avg_ms_x10 / 10;
    if (jit_ms > 3) { int32_t ex = (int32_t)jit_ms - 3; if (ex > 17) ex = 17; score -= (ex * 30) / 17; }
    if (ls->window_pkts > 0 && ls->window_rpd_sum * 2 < ls->window_pkts) score -= 20;
    if (score < 0) score = 0;
    if (score > 100) score = 100;
    ls->link_quality = (uint8_t)score;

    LinkStats_ResetWindow(ls);
}

static void LinkStats_Log(const NRF_LinkStats *ls)
{
    LOG("NRF", "LinkStats", "INFO",
        "loss=%u.%u%% rate=%u/s jit=%u.%ums pk=%ums rpd=%u lq=%u",
        ls->loss_pct_x10 / 10, ls->loss_pct_x10 % 10, ls->arrival_rate,
        ls->jitter_avg_ms_x10 / 10, ls->jitter_avg_ms_x10 % 10,
        ls->jitter_max_ms, ls->rpd, ls->link_quality);
}
/*======================================================================================================*/

/*============== NRF INIT WITH RETRY ===================================================================*/
static NRF_Status NRF_InitWithRetry(void)
{
    LOG("CM4", "NRF_Init", "INFO", "ch=%u addr=E7:E7:E7:E7:E7 POR=%ums", NRF_RF_CHANNEL, NRF_INIT_POR_DELAY_MS);
    HAL_Delay(NRF_INIT_POR_DELAY_MS);

    NRF_Status ret = NRF_ERR_NOT_DETECTED;
    for (int attempt = 0; attempt < NRF_INIT_MAX_RETRIES; attempt++)
    {
        HAL_IWDG_Refresh(&hiwdg2);
        ret = NRF_Init(&g_hnrf, &hspi1, NRF_RF_CHANNEL, nrf_address);
        if (ret == NRF_OK)
        {
            LOG("CM4", "NRF_Init", "OK", "radio up (attempt %d/%d)", attempt + 1, NRF_INIT_MAX_RETRIES);
            return NRF_OK;
        }
        LOG("CM4", "NRF_Init", "WARN", "attempt %d/%d failed ret=%d — backoff %dms",
            attempt + 1, NRF_INIT_MAX_RETRIES, ret, NRF_INIT_BACKOFF_MS * (attempt + 1));
        HAL_Delay(NRF_INIT_BACKOFF_MS * (attempt + 1));
    }
    LOG("CM4", "NRF_Init", "FAIL", "all %d attempts failed (ret=%d)", NRF_INIT_MAX_RETRIES, ret);
    return ret;
}
/*======================================================================================================*/

/*============== HAL CALLBACK ==========================================================================*/
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
    if (GPIO_Pin == NRF_IRQ_Pin)  nrf_irq_flag = true;
    if (GPIO_Pin == E_STOP_Pin)   estop_flag   = true;
}
/*======================================================================================================*/

/*============== CPCU_CM4_Init =========================================================================*/
void CPCU_CM4_Init(void)
{
    LOG("CM4", "CPCU_CM4_Init", "RUN", "=================================================");
    LOG("CM4", "CPCU_CM4_Init", "INFO", "======= InfiniTech CPCU — CM4 Online =======");

    /* ── IWDG2 recovery or cold boot handshake ── */
    if (g_iwdg2_recovery)
    {
        LOG("CM4", "CPCU_CM4_Init", "WARN", "IWDG2 warm recovery detected");
        IPC_DIAG()->m4_watchdog_events++;

        volatile IPC_ControlBlock *ctrl = IPC_CTRL();
        if (ctrl->magic != IPC_MAGIC)
        {
            LOG("CM4", "CPCU_CM4_Init", "FAIL", "IPC magic invalid — cold reset needed");
            EnterSafeState();
        } else
        {
            LOG("CM4", "CPCU_CM4_Init", "OK", "IPC magic=0x%08lX — re-syncing", (unsigned long)ctrl->magic);
            ctrl->sensor_head  = ctrl->sensor_tail;
            ctrl->m4_ready     = 1;
            ctrl->system_state = IPC_STATE_RUNNING;
            LOG("CM4", "CPCU_CM4_Init", "OK", "m4_ready=1, state=RUNNING (warm)");
        }
    } else
    {
        LOG("CM4", "IPC_Handshake", "INFO", "waiting for CM7...");
        while (!IPC_IsReady()) HAL_IWDG_Refresh(&hiwdg2);
        LOG("CM4", "IPC_Handshake", "OK", "CM7 ready, magic=0x%08lX", (unsigned long)IPC_CTRL()->magic);
        IPC_CTRL()->m4_ready     = 1;
        IPC_CTRL()->system_state = IPC_STATE_RUNNING;
        LOG("CM4", "IPC_Handshake", "OK", "m4_ready=1, state=RUNNING (cold)");
    }

    /* ── NRF init with retry (Rev 6.0) ── */
    NRF_Status nrf_ret          = NRF_InitWithRetry();
    IPC_DIAG()->m4_nrf_init_status = (uint32_t)nrf_ret;

    if (nrf_ret != NRF_OK)
    {
        /* DEGRADED, not permanent SAFE — main loop retries NRF_Init() */
        IPC_CTRL()->system_state = IPC_STATE_SAFE;
        radio_state              = RADIO_DEGRADED;
        degraded_enter_tick      = HAL_GetTick();
        g_nrf_init_ok            = false;
        ServosToNeutral();
        LOG("CM4", "NRF_Init", "WARN", "entered DEGRADED — will retry every %ums", NRF_REINIT_INTERVAL_MS);
    } else
    {
        g_nrf_init_ok = true;
    }

    /* ── Link quality + PWM ── */
    LinkStats_Init(&g_link_stats);
    LOG("CM4", "LinkStats", "OK", "window=%ums expected=%upkt/s", LINK_STATS_WINDOW_MS, LINK_EXPECTED_PKT_RATE);

    HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_1);
    HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_2);
    HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_3);
    HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_4);
    HAL_TIM_PWM_Start(&htim8, TIM_CHANNEL_1);
    HAL_TIM_PWM_Start(&htim8, TIM_CHANNEL_2);
    LOG("CM4", "PWM_Start", "OK", "6ch at %uus neutral", SERVO_NEUTRAL);

    last_valid_pkt_tick = HAL_GetTick();
    if (g_nrf_init_ok) radio_state = RADIO_RUNNING;

    LOG("CM4", "CPCU_CM4_Init", "OK", "=================================================");
    LOG("CM4", "CPCU_CM4_Init", "INFO", "radio=%s nrf=%d state=%u timeout=%ums",
        g_nrf_init_ok ? "OK" : "FAIL", nrf_ret, IPC_CTRL()->system_state, RADIO_TIMEOUT_MS);
}
/*======================================================================================================*/

/*============== CPCU_CM4_Run ==========================================================================*/
void CPCU_CM4_Run(void)
{
#if defined(CPCU_MODE_TEST_LOG) || defined(CPCU_MODE_TEST_CSV)
    CPCU_CM4_TestMain();
#endif

    LOG("CM4", "CPCU_CM4_Run", "RUN", "entering reflex loop");

    while (1)
    {
        /* 1. E-STOP — with double-entry guard (Rev 6.0) */
        if (estop_flag && radio_state != RADIO_SAFE)
        {
            LOG("CM4", "E_STOP", "WARN", "E-STOP asserted on PC13");
            EnterSafeState();
        }

        /* 2. Radio timeout state machine */
        if (radio_state == RADIO_RUNNING)
        {
            if ((HAL_GetTick() - last_valid_pkt_tick) > RADIO_TIMEOUT_MS)
                EnterDegradedState();
        }
        else if (radio_state == RADIO_DEGRADED)
        {
            /* NRF init failed at boot → periodic re-init (Rev 6.0) */
            if (!g_nrf_init_ok)
            {
                if ((HAL_GetTick() - g_last_reinit_tick) > NRF_REINIT_INTERVAL_MS)
                {
                    g_last_reinit_tick = HAL_GetTick();
                    LOG("CM4", "NRF_ReInit", "INFO", "re-init attempt...");
                    NRF_Status ret = NRF_Init(&g_hnrf, &hspi1, NRF_RF_CHANNEL, nrf_address);
                    IPC_DIAG()->m4_nrf_init_status = (uint32_t)ret;
                    if (ret == NRF_OK)
                    {
                        LOG("CM4", "NRF_ReInit", "OK", "NRF recovered — listening");
                        g_nrf_init_ok       = true;
                        last_valid_pkt_tick = HAL_GetTick();
                        degraded_enter_tick = HAL_GetTick();
                    } else
                    {
                        LOG("CM4", "NRF_ReInit", "WARN", "still failing (ret=%d)", ret);
                    }
                }
            }
            else if ((HAL_GetTick() - degraded_enter_tick) > RADIO_TIMEOUT_MS)
            {
                LOG("CM4", "RadioTimeout", "WARN", "sustained loss %lums — SAFE", (unsigned long)(RADIO_TIMEOUT_MS * 2));
                EnterSafeState();
            }
        }

        /* 3. NRF IRQ: drain RX FIFO */
        if (nrf_irq_flag && radio_state != RADIO_SAFE)
        {
            nrf_irq_flag = false;
            while (NRF_DataAvailable(&g_hnrf))
            {
                if (NRF_ReadPayload(&g_hnrf, nrf_raw_buf) == NRF_OK)
                {
                    WL_Unpack(nrf_raw_buf, &rx_pkt);

                    if (rx_pkt.flags & WL_FLAG_FIRST_PACKET)
                    {
                        expected_seq = rx_pkt.seq;
                        LOG("NRF", "FirstPacket", "INFO", "BSAU boot — seq sync %u", rx_pkt.seq);
                    }

                    LinkStats_FeedPacket(&g_link_stats, &g_hnrf, rx_pkt.seq, expected_seq);

                    bool seq_ok = (rx_pkt.seq == expected_seq);
                    if (!seq_ok)
                    {
                        IPC_DIAG()->m4_seq_gaps++;
                        LOG("NRF", "SeqGap", "WARN", "exp=%u got=%u lost=%u", expected_seq, rx_pkt.seq, (uint8_t)(rx_pkt.seq - expected_seq));
                    }
                    expected_seq = rx_pkt.seq + 1;

                    /* Flags */
                    uint8_t batt = WL_BATT_GET(rx_pkt.flags);
                    if (batt == WL_BATT_CRITICAL)     LOG("NRF", "Flags", "WARN", "BSAU battery CRITICAL");
                    else if (batt == WL_BATT_LOW)     LOG("NRF", "Flags", "WARN", "BSAU battery low");
                    if (rx_pkt.flags & WL_FLAG_ELECTRODE_OFF) LOG("NRF", "Flags", "WARN", "electrode disconnected");
                    if (rx_pkt.flags & WL_FLAG_ADC_OVERRUN)   LOG("NRF", "Flags", "WARN", "ADC overrun");
                    if (rx_pkt.flags & WL_FLAG_TX_SATURATED)  LOG("NRF", "Flags", "WARN", "TX saturated");
                    if (rx_pkt.flags & WL_FLAG_CLIPPING)      LOG("NRF", "Flags", "INFO", "signal clipping");

                    IPC_M4_PushSensor((const WL_Packet *)&rx_pkt);

                    LOG("NRF", "RxPacket", "OK", "seq=%u f=0x%02X ch0-2=%u,%u,%u",
                        rx_pkt.seq, rx_pkt.flags, rx_pkt.samples[0].ch[0], rx_pkt.samples[0].ch[1], rx_pkt.samples[0].ch[2]);

                    last_valid_pkt_tick = HAL_GetTick();

                    if (radio_state == RADIO_DEGRADED)
                    {
                        radio_state        = RADIO_RECOVERING;
                        recovery_pkt_count = seq_ok ? 1 : 0;
                        LOG("CM4", "RadioRecovering", "INFO", "pkt arrived — recovery started");
                    }
                    else if (radio_state == RADIO_RECOVERING)
                    {
                        if (seq_ok) { recovery_pkt_count++; if (recovery_pkt_count >= RECOVERY_PKT_COUNT) EnterRunningState(); }
                        else        { recovery_pkt_count = 0; LOG("CM4", "RadioRecovering", "WARN", "gap — count reset"); }
                    }
                }
            }
            NRF_ClearIRQ(&g_hnrf, NRF_STATUS_RX_DR);
        }

        /* 4. Motor command */
        if (radio_state == RADIO_RUNNING && IPC_M4_ReadMotorCommand(&motor_cmd))
        {
            if (motor_cmd.seq <= min_valid_cmd_seq)
            {
                LOG("CM4", "MotorCmd", "WARN", "stale seq=%lu need>%lu", (unsigned long)motor_cmd.seq, (unsigned long)min_valid_cmd_seq);
            } else
            {
                LOG("CM4", "MotorCmd", "OK", "g=%u c=%u%% s=[%u,%u,%u,%u,%u,%u]",
                    motor_cmd.gesture_id, motor_cmd.confidence,
                    motor_cmd.servo_us[0], motor_cmd.servo_us[1], motor_cmd.servo_us[2],
                    motor_cmd.servo_us[3], motor_cmd.servo_us[4], motor_cmd.servo_us[5]);
                SafetyClamp(motor_cmd.servo_us);
                __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_1, us_to_ccr(motor_cmd.servo_us[0]));
                __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_2, us_to_ccr(motor_cmd.servo_us[1]));
                __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_3, us_to_ccr(motor_cmd.servo_us[2]));
                __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_4, us_to_ccr(motor_cmd.servo_us[3]));
                __HAL_TIM_SET_COMPARE(&htim8, TIM_CHANNEL_1, us_to_ccr(motor_cmd.servo_us[4]));
                __HAL_TIM_SET_COMPARE(&htim8, TIM_CHANNEL_2, us_to_ccr(motor_cmd.servo_us[5]));
            }
        }

        /* 5. Link quality — once per window */
        if ((HAL_GetTick() - g_link_stats.window_start_tick) >= LINK_STATS_WINDOW_MS)
        {
            LinkStats_ComputeWindow(&g_link_stats);
            LinkStats_Log(&g_link_stats);
        }

        /* 6. Watchdog */
        HAL_IWDG_Refresh(&hiwdg2);
    }
}
/*==========================================================================================================*/
