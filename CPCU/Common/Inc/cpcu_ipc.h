/*
 * @file    cpcu_ipc.h
 * @brief   Inter-Processor Communication via SRAM4 (D3 domain)
 * @author  bugrASl
 *
 * Manages shared memory between CM4 (Reflex Layer) and CM7 (Cognitive Layer)
 * on the STM32H755ZI-Q. Data flows in two directions:
 *
 *   CM4 → CM7:  Sensor ring buffer     (raw EMG samples from NRF radio)
 *   CM7 → CM4:  Motor command buffer   (servo targets from ML inference)
 *
 * Memory map of the 64 KB SRAM4 (0x38000000 - 0x3800FFFF):
 *
 *          ┌──────────────────────────────────────────────────┐ 0x38000000
 *          │  IPC_ControlBlock                                │
 *          │    magic, version, ready flags, head/tail,       │
 *          │    motor command seq/ack, system state           │
 *          ├──────────────────────────────────────────────────┤
 *          │  Sensor Ring Buffer (M4 writes → M7 reads)       │
 *          │    IPC_SENSOR_RING_SIZE entries                  │
 *          │    Each entry: WL_SAMPLES_PER_PACKET sample sets │
 *          │    of WL_NUM_CHANNELS channels + metadata        │
 *          ├──────────────────────────────────────────────────┤
 *          │  Motor Command Buffer (M7 writes → M4 reads)     │
 *          │    Latest-wins: only the most recent command     │
 *          │    6 servo targets + gesture info + sequence     │
 *          ├──────────────────────────────────────────────────┤
 *          │  Diagnostics / Debug Counters                    │
 *          │    Packet counts, overflow/underflow, latency    │
 *          ├──────────────────────────────────────────────────┤
 *          │  [Reserved — remaining SRAM4 space]              │
 *          └──────────────────────────────────────────────────┘ 0x3800FFFF
 *
 * Synchronization:
 *   HSEM ID 0 — Notification: M4 wrote new sensor data (wakes M7)
 *   HSEM ID 1 — Notification: M7 wrote new motor command (wakes M4)
 *   HSEM ID 2 — Boot synchronization (CM7 releases CM4 from STOP mode)
 *   All used as doorbells (take+release), NOT as mutexes.
 *
 * Design principles:
 *   - SPSC (single-producer single-consumer) for each buffer direction
 *   - Lock-free: M4 only writes sensor_head, M7 only writes sensor_tail
 *   - __DMB() barriers ensure write ordering across cores
 *   - SRAM4 configured non-cacheable via MPU (Region 3 on CM7, Region 1
 *     on CM4) so both cores see writes immediately — no cache maintenance
 */

#ifndef CPCU_IPC_H
#define CPCU_IPC_H

#ifdef __cplusplus
extern "C" {
#endif

#include "stm32h7xx_hal.h"
#include "wireless_packet.h"
#include <stdint.h>
#include <stdbool.h>

/*============== SHARED MEMORY LAYOUT CONSTANTS ============================================================*/
#define IPC_SHM_BASE            0x38000000UL
#define IPC_SHM_SIZE            0x10000UL                           /* 64 KB */
#define IPC_MAGIC               0x494E4654UL                        /* "INFT" — InfiniTech */
#define IPC_VERSION             0x0100                              /* v1.0 */
 
/*
 * Sensor ring buffer depth. Must be power of 2 for fast modulo (& mask).
 */
#define IPC_SENSOR_RING_SIZE    128
#define IPC_SENSOR_RING_MASK    (IPC_SENSOR_RING_SIZE - 1)
 
/* Ring size must be power of 2 */
#if (IPC_SENSOR_RING_SIZE & IPC_SENSOR_RING_MASK) != 0
  #error "IPC_SENSOR_RING_SIZE must be a power of 2."
#endif
 
/** Number of servo channels */
#define IPC_NUM_SERVOS          6
/*==========================================================================================================*/

/*============== IPC CONSTANTS =============================================================================*/
#define IPC_HSEM_SENSOR     0                   /* M4 → M7: new sensor data available */
#define IPC_HSEM_MOTOR      1                   /* M7 → M4: new motor command available */
#define IPC_HSEM_UART       3                   /* UART mutex: prevents garbled output */
 
/* System States */
#define IPC_STATE_INIT      0                   /* Boot: IPC initialized, waiting for both cores */
#define IPC_STATE_RUNNING   1                   /* Normal operation */
#define IPC_STATE_SAFE      2                   /* Watchdog timeout or E-Stop: servos at neutral */
/*==========================================================================================================*/

/*============== DATA STRUCTURES ===========================================================================*/ 
typedef struct __attribute__((aligned(4))) {
    uint32_t            magic;                  /* IPC_MAGIC — written by CM7, checked by CM4 */
    uint16_t            version;                /* IPC_VERSION */
    uint8_t             m7_ready;               /* 1 = CM7 finished IPC_Init() */
    uint8_t             m4_ready;               /* 1 = CM4 verified and is running */
 
    volatile uint32_t   sensor_head;            /* Next write index (M4 owns) */
    volatile uint32_t   sensor_tail;            /* Next read index (M7 owns) */
 
    volatile uint32_t   motor_cmd_seq;          /* Incremented by M7 per new command */
    volatile uint32_t   motor_cmd_ack;          /* Last seq acknowledged by M4 */
 
    volatile uint8_t    system_state;           /* IPC_STATE_INIT / RUNNING / SAFE */
    uint8_t             _pad[3];
 
    uint32_t            _reserved[9];           /* Pad to 64 bytes total */
} IPC_ControlBlock;
 
typedef struct __attribute__((aligned(4))) {
    WL_Sample   samples[WL_SAMPLES_PER_PACKET]; /* Unpacked ADC data */
    uint8_t     seq;                            /* Packet sequence number (loss detection) */
    uint8_t     flags;                          /* BSAU status flags (WL_FLAG_*) */
    uint16_t    _pad;                           /* Alignment padding */
    uint32_t    timestamp;                      /* M4's HAL_GetTick() at reception time */
} IPC_SensorEntry;
 
typedef struct __attribute__((aligned(4))) {
    uint16_t    servo_us[IPC_NUM_SERVOS];       /* Target pulse width µs (500-2500) */
    uint8_t     gesture_id;                     /* Classified gesture ID (for debug) */
    uint8_t     confidence;                     /* Classifier confidence 0-100% */
    uint32_t    seq;                            /* Monotonic counter */
    uint32_t    timestamp;                      /* M7's HAL_GetTick() at inference completion */
} IPC_MotorCommand;
 
/*
 * Diagnostic counters for monitoring IPC health.
 * Each field has exactly one writer core to avoid contention.
 *
 * Rev 6.0: Added m4_safe_state_entries and m4_nrf_init_status.
 *          Reduced _reserved from 8 to 6 to maintain 64-byte struct.
 */
typedef struct __attribute__((aligned(4))) {
    /* Written by M4 only */
    uint32_t    m4_pkts_received;               /* Total valid radio packets */
    uint32_t    m4_pkts_dropped;                /* Packets dropped (any reason) */
    uint32_t    m4_ring_overflows;              /* Ring full — oldest overwritten */
    uint32_t    m4_seq_gaps;                    /* Sequence number discontinuities */
    uint32_t    m4_watchdog_events;             /* IWDG2 watchdog resets only */
    uint32_t    m4_safe_state_entries;           /* Times EnterSafeState() called (Rev 6.0) */
    uint32_t    m4_nrf_init_status;             /* Last NRF_Init() return value (Rev 6.0) */
 
    /* Written by M7 only */
    uint32_t    m7_batches_processed;           /* DSP+ML inference cycles completed */
    uint32_t    m7_max_latency_ms;              /* Worst-case inference time in ms */
    uint32_t    m7_ring_underflows;             /* Ring empty when M7 tried to read */
 
    /* Pad to 64 bytes */
    uint32_t    _reserved[6];
} IPC_Diagnostics;
/*==========================================================================================================*/

/*============== COMPILE-TIME VALIDATIONS ==================================================================*/
_Static_assert(sizeof(IPC_ControlBlock) == 64,
    "IPC_ControlBlock size changed — update memory map");

_Static_assert(sizeof(IPC_Diagnostics) == 64,
    "IPC_Diagnostics size changed — update memory map");

_Static_assert(sizeof(IPC_MotorCommand) <= 32,
    "IPC_MotorCommand exceeds 32 bytes");

_Static_assert(
    sizeof(IPC_ControlBlock)
    + sizeof(IPC_SensorEntry) * IPC_SENSOR_RING_SIZE
    + sizeof(IPC_MotorCommand)
    + sizeof(IPC_Diagnostics)
    <= 0x10000,
    "Total IPC layout exceeds 64 KB SRAM4"
);
/*==========================================================================================================*/

/*============== SHARED MEMORY REGION POINTER ACCESS MACROS ================================================*/
#define IPC_CTRL() \
    ((volatile IPC_ControlBlock *)(IPC_SHM_BASE))
 
#define IPC_SENSOR_RING() \
    ((volatile IPC_SensorEntry *)(IPC_SHM_BASE \
     + sizeof(IPC_ControlBlock)))
 
#define IPC_MOTOR_CMD() \
    ((volatile IPC_MotorCommand *)(IPC_SHM_BASE \
     + sizeof(IPC_ControlBlock) \
     + sizeof(IPC_SensorEntry) * IPC_SENSOR_RING_SIZE))
 
#define IPC_DIAG() \
    ((volatile IPC_Diagnostics *)(IPC_SHM_BASE \
     + sizeof(IPC_ControlBlock) \
     + sizeof(IPC_SensorEntry) * IPC_SENSOR_RING_SIZE \
     + sizeof(IPC_MotorCommand)))
/*==========================================================================================================*/

/*==========================================================================================================*/
/*-------------- PUBLIC API SECTION ------------------------------------------------------------------------*/
/*==========================================================================================================*/
 
void IPC_Init(void);
bool IPC_IsReady(void);
 
bool IPC_M4_PushSensor(const WL_Packet *pkt);
bool IPC_M4_ReadMotorCommand(IPC_MotorCommand *cmd);
 
bool IPC_M7_PopSensor(IPC_SensorEntry *entry);
bool IPC_M7_PopSensorBatch(IPC_SensorEntry *entries, uint32_t *count);
void IPC_M7_PushMotorCommand(const IPC_MotorCommand *cmd);
 
uint32_t IPC_SensorCount(void);
/*==========================================================================================================*/

#ifdef __cplusplus
}
#endif
 
#endif /* CPCU_IPC_H */
