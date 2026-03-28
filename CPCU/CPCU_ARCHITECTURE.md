# CPCU_ARCHITECTURE.md — InfiniTech CPCU / BSAU System Architecture

**Project:**  EMG-Driven Prosthetic Hand Controller
**MCU:**      STM32H755ZI-Q (Dual-Core Cortex-M7 + Cortex-M4)
**Board:**    NUCLEO-H755ZI-Q
**Author:**   bugrASl
**Date:**     2026-03-22
**Revision:** 6.0 (2026-03-28 — NRF init retry + POR delay, degraded-not-safe on init fail, NRF re-init in main loop, E-STOP guard, diagnostic counter fixes, enhanced NRF tests, unified log.h, TEST_LOG/TEST_CSV split)

---

## 1. System Overview

Two boards form a prosthetic hand controller:

**BSAU (STM32L432KC)** — acquires 6-channel EMG at 2 kHz, packs into 32-byte NRF24L01+ payloads, transmits at 250 kbps.

**CPCU (STM32H755ZI-Q)** — receives EMG (CM4), runs DSP+ML classification (CM7), drives 6 servos (CM4), enforces safety watchdogs on both cores.

```
EMG → BSAU ADC → NRF TX → [AIR] → NRF RX → CM4 → IPC → CM7 → IPC → CM4 → PWM → Servos
```

---

## 2. Hardware

### 2.1 Pin Map

| Pin  | Function       | Core | Notes                          |
|------|----------------|------|--------------------------------|
| PA5/6/7 | SPI1 SCK/MISO/MOSI | CM4 | NRF24L01+ (~1.5 MHz)     |
| PD14 | NRF_CSN (GPIO) | CM4  | Chip select, active low        |
| PD15 | NRF_CE (GPIO)  | CM4  | Chip enable                    |
| PG6  | NRF_IRQ (EXTI) | CM4  | Falling edge, pull-up          |
| PC13 | E_STOP (EXTI)  | CM4  | Rising edge                    |
| PE9/11/13/14 | TIM1 CH1-4 | CM4 | Servo 0-3 PWM              |
| PC6/7 | TIM8 CH1-2   | CM4  | Servo 4-5 PWM                 |
| PD8/9 | USART3 TX/RX | Both | ST-LINK VCP, HSEM-protected    |

### 2.2 Peripheral Ownership

| Peripheral | Core | Purpose                    |
|------------|------|----------------------------|
| SPI1       | CM4  | NRF24L01+                  |
| TIM1/TIM8  | CM4  | Servo PWM (6 channels)     |
| TIM6/TIM7  | CM7/CM4 | HAL timebase (1 ms)     |
| IWDG1/IWDG2| CM7/CM4 | Hardware watchdog (1 s) |
| USART3     | Both | Debug UART (115200 8N1)    |
| HSEM       | Both | IPC doorbells + UART mutex |

---

## 3. Software Architecture

### 3.1 Module Graph

```
main_CM7.c → cpcu_cm7_app.h/c → log.h
                  │                  │
                  ├→ cpcu_test.h/c ──┘
                  └→ cpcu_ipc.h/c → wireless_packet.h/c

main_CM4.c → cpcu_cm4_app.h/c → log.h
                  │                  │
                  ├→ cpcu_test.h/c ──┘
                  ├→ cpcu_ipc.h/c
                  ├→ nrf24l01.h/c → nrf24l01_test.h/c
                  └→ wireless_packet.h/c
```

### 3.2 File Ownership

| File                  | Compiled By    | Notes                         |
|-----------------------|----------------|-------------------------------|
| `cpcu_cm7_app.c/h`   | CM7 only       | Cognitive layer                |
| `cpcu_cm4_app.c/h`   | CM4 only       | Reflex layer                   |
| `cpcu_ipc.c/h`       | Both cores     | Lock-free SPSC ring buffer     |
| `log.h`              | Both cores     | Header-only LOG/LOG_CSV macros |
| `cpcu_test.c/h`      | Both cores     | On-target test harness         |
| `wireless_packet.c/h`| Both + BSAU    | 12-bit pack/unpack codec       |
| `nrf24l01.c/h`       | CM4 + BSAU     | Unified NRF driver             |
| `nrf24l01_test.c/h`  | CM4 + BSAU     | NRF self-test suite            |

All custom code in `cpcu_*.c/h` and `nrf24l01*.c/h`. CubeMX-generated `main.c` has only 3-4 USER CODE lines. CubeMX can regenerate freely.

---

## 4. CM7 — Cognitive Layer

**Clock:** 400 MHz (VOS1, Direct SMPS) · **HAL Timebase:** TIM6 · **Flash:** Bank 1 (0x08000000) · **RAM:** AXI SRAM (0x24000000, 512 KB)

Responsibilities: system clock init, IPC init, CM4 release via HSEM, sensor batch consumption, threshold classification (DSP/ML placeholder), motor command generation, diagnostics (1/sec), IWDG1 refresh.

**Threshold classifier:** 3 thresholds (800/1600/2800) divide 12-bit ADC into 4 zones → 4 servo positions (1500/1833/2166/2500 µs). Only latest ring entry processed. CM4's `SafetyClamp()` constrains per-joint.

### 4.1 CM7 MPU

| Region | Base       | Size   | TEX/Share/Cache/Buffer | Purpose         |
|--------|------------|--------|------------------------|-----------------|
| 0      | 0x00000000 | 4 GB   | Background fault       | No access       |
| 1      | 0x08000000 | 1 MB   | C, NB, NS              | CM7 Flash       |
| 2      | 0x20000000 | 128 KB | C, NB, NS              | DTCM            |
| 3      | 0x38000000 | 64 KB  | NC, S (TEX=1)          | IPC SRAM4       |
| 4      | 0x24000000 | 512 KB | C, B, NS               | AXI SRAM        |
| 5      | 0x40000000 | 512 MB | NC, B, S               | Peripherals     |

> **Region 5 is critical.** Without it, any peripheral access causes MemManage fault.

---

## 5. CM4 — Reflex Layer

**Clock:** 200 MHz (HPRE /2) · **HAL Timebase:** TIM7 · **Flash:** Bank 2 (0x08100000) · **RAM:** SRAM1+2 alias (0x10000000, 288 KB)

Responsibilities: NRF radio RX (EXTI-driven), packet decoding, flags processing, IPC push, motor command consumption, stale command rejection, per-joint safety clamping, PWM actuation, E-STOP, radio timeout state machine, link quality monitoring, IWDG2 warm recovery, NRF init retry with POR delay (Rev 6.0), NRF re-init in DEGRADED state (Rev 6.0).

### 5.1 CM4 MPU

| Region | Base       | Size   | TEX/Share/Cache/Buffer | Purpose     |
|--------|------------|--------|------------------------|-------------|
| 0      | 0x08100000 | 1 MB   | C, NB, NS              | CM4 Flash   |
| 1      | 0x38000000 | 64 KB  | NC, S (TEX=1)          | IPC SRAM4   |
| 2      | 0x10000000 | 256 KB | C, NB, NS              | SRAM1+2     |
| 3      | 0x40000000 | 512 MB | NC, B, S               | Peripherals |

### 5.2 NVIC Priority Map

| Priority | Interrupts                          | Rationale                |
|----------|-------------------------------------|--------------------------|
| 0        | TIM1_BRK, TIM8_BRK, EXTI15_10      | Safety (E-STOP, break)   |
| 1        | EXTI9_5                             | NRF radio IRQ            |
| 2        | SPI1, TIM1_UP, TIM8_UP              | Data + PWM               |
| 3        | HSEM2                               | IPC notification         |

---

## 6. Inter-Processor Communication

### 6.1 SRAM4 Layout (0x38000000, 64 KB)

```
IPC_ControlBlock (64B) → Sensor Ring (128 × IPC_SensorEntry) → Motor Command (≤32B) → Diagnostics (64B) → [reserved]
```

### 6.2 HSEM Allocation

| ID | Direction | Purpose                           |
|----|-----------|-----------------------------------|
| 0  | M4→M7     | Sensor data available (doorbell)  |
| 1  | M7→M4     | Motor command available (doorbell)|
| 2  | M7→M4     | Boot synchronization              |
| 3  | Both      | UART mutex (message-level lock)   |

### 6.3 Lock-Free SPSC

M4 writes `sensor_head`, M7 writes `sensor_tail`. SRAM4 is non-cacheable (MPU). `__DMB()` barriers enforce ordering. Ring is 128 slots, capacity 127 (one slot sacrificed). Motor command is latest-wins via `seq/ack` protocol.

### 6.4 Diagnostics (Rev 6.0)

| Field                  | Writer | Purpose                              |
|------------------------|--------|--------------------------------------|
| `m4_pkts_received`     | CM4    | Total valid radio packets            |
| `m4_pkts_dropped`      | CM4    | Packets dropped                      |
| `m4_ring_overflows`    | CM4    | Ring full — oldest overwritten       |
| `m4_seq_gaps`          | CM4    | Sequence discontinuities             |
| `m4_watchdog_events`   | CM4    | IWDG2 resets only                    |
| `m4_safe_state_entries`| CM4    | Times `EnterSafeState()` called      |
| `m4_nrf_init_status`   | CM4    | Last `NRF_Init()` return value       |
| `m7_batches_processed` | CM7    | DSP inference cycles                 |
| `m7_max_latency_ms`    | CM7    | Worst-case inference time            |
| `m7_ring_underflows`   | CM7    | Ring empty on read attempt           |

---

## 7. NRF24L01+ Wireless Subsystem

| Parameter    | Value                             |
|--------------|-----------------------------------|
| Data rate    | 250 kbps (best sensitivity −94 dBm) |
| TX power     | 0 dBm (max)                       |
| Channel      | 76                                |
| Address      | E7:E7:E7:E7:E7 (5 bytes)         |
| Payload      | 32 bytes fixed                    |
| CRC          | 2 bytes hardware                  |
| Auto-ACK     | Pipe 0, 15 retries × 1500 µs     |

Unified driver: `NRF_ROLE_TX` (BSAU) or `NRF_ROLE_RX` (CPCU CM4).

### 7.1 NRF Init with Retry (Rev 6.0)

**Problem:** NRF24L01+ needs ≤100 ms POR from VDD. Without delay, SPI readback fails → `NRF_ERR_NOT_DETECTED` → old code entered permanent SAFE state at boot.

**Fix:** 200 ms POR delay before first SPI access, up to 3 attempts with progressive backoff (100/200/300 ms). If all fail → DEGRADED (recoverable), not SAFE. Main loop retries `NRF_Init()` every 3 s while in DEGRADED.

### 7.2 Link Quality Monitoring

1-second rolling window: loss rate, arrival rate, jitter, RPD, composite score (0–100). Output via LOG once/sec in DEBUG/TEST_LOG; zero cost in RELEASE.

---

## 8. Wireless Packet Protocol

### 8.1 Wire Format (32 bytes)

```
[0] seq  [1] flags  [2-10] sample0 (6ch×12bit)  [11-19] sample1  [20-28] sample2  [29-31] reserved
```

12-bit packing: 2 channels per 3 bytes. 3 samples/packet × ~667 pkt/s = 2 kHz effective rate.

### 8.2 Flags Byte

```
bit 7:FIRST_PACKET  6:CLIPPING  5:ELEC_OFF  4:ADC_OVRN  3:TX_SAT  2:CAL  1:0:BATT_LVL
```

Battery: 0=OK, 1=LOW, 2=CRITICAL, 3=CHARGING. FIRST_PACKET resets `expected_seq` on BSAU boot.

---

## 9. Safety Architecture

### 9.1 Layers

| Layer          | Mechanism                          | Response               | Recovery        |
|----------------|------------------------------------|------------------------|-----------------|
| E-STOP         | EXTI PC13 (prio 0)                | Permanent safe state   | Reset only      |
| TIM Break      | TIM1/TIM8 break (prio 0)          | PWM idle               | Auto            |
| Radio timeout  | Software state machine             | See §9.2               | Auto-recovery   |
| Per-joint clamp| `SafetyClamp()` per command        | Pulse clamped          | Continuous      |
| IWDG2          | 1 s watchdog on CM4               | D2 domain reset        | Warm recovery   |
| IWDG1          | 1 s watchdog on CM7               | Full system reset      | Cold reboot     |

### 9.2 Radio State Machine (Rev 6.0)

```
RUNNING ──(750ms no pkt)──→ DEGRADED ──(750ms more)──→ SAFE (permanent)
    ↑                            │
    │                            │ (pkt arrives)
    │                            ▼
    └──(10 consecutive ok)── RECOVERING
```

**NRF init failure at boot (Rev 6.0):** enters DEGRADED, not SAFE. Main loop retries `NRF_Init()` every 3 s. On success → `last_valid_pkt_tick` reset → normal timeout path.

**E-STOP guard (Rev 6.0):** `estop_flag` only processed if `radio_state != RADIO_SAFE` to prevent infinite re-entry.

### 9.3 Per-Joint Limits (µs)

| Joint | Min  | Max  | Neutral |
|-------|------|------|---------|
| 0     | 600  | 2400 | 1500    |
| 1     | 600  | 2200 | 1500    |
| 2     | 500  | 2400 | 1500    |
| 3     | 600  | 2400 | 1500    |
| 4     | 500  | 2400 | 1500    |
| 5     | 700  | 2300 | 1500    |

### 9.4 IWDG2 Warm Recovery

CM4 `main.c` checks `RCC_FLAG_IWDG2RST` → skips HSEM/STOP → verifies IPC magic → re-syncs ring → re-inits NRF + PWM → resumes.

---

## 10. Boot Sequence

```
CM7:  MPU → Cache → HAL → Clocks → IPC_Init() → HSEM release → CPCU_CM7_Init() → Run()
CM4:  MPU → HSEM wait (STOP) → wake → HAL → peripherals → CPCU_CM4_Init() → Run()
```

`IPC_Init()` MUST precede HSEM release. IWDG2 warm recovery skips STOP (§9.4).

---

## 11. Clock Tree

```
HSE 8 MHz → PLL1 → 400 MHz SYSCLK → D1CPRE/1 → 400 MHz CM7
                                    → HPRE/2   → 200 MHz CM4/AHB
                                    → APB1-4/2 → 100 MHz periph (200 MHz timers)
LSI 32 kHz → IWDG1/2: PSC/128, reload 1000 → 1 s timeout
```

TIM1/TIM8: PSC=199, ARR=19999 → 50 Hz, 1 count = 1 µs. SPI1: ~1.5 MHz SCK.

---

## 12. Build Mode System

```
                    RELEASE    DEBUG      TEST_LOG     TEST_CSV
LOG()               off        on         on           off
LOG_CSV()           off        on         off          on
CM7 Run()           normal     normal     test suite   CSV monitor
CM4 Run()           normal     normal     NRF tests +  NRF tests +
                                          hybrid loop  hybrid loop
BSAU required       yes        yes        no           no
```

`log.h` provides both macros. Board identity: `LOG_BOARD_CPCU` or `LOG_BOARD_BSAU`. CPCU uses `printf` + HSEM; BSAU uses `snprintf` + `HAL_UART_Transmit`. Zero cost in RELEASE.

### 12.1 CubeIDE Preprocessor

**CM7:** `CPCU_MODE_TEST_LOG` (or `_DEBUG`/`_RELEASE`/`_TEST_CSV`), `CORE_CM7`, `LOG_BOARD_CPCU`, `USE_HAL_DRIVER`, `STM32H755xx`, `USE_PWR_DIRECT_SMPS_SUPPLY`

**CM4:** Same mode + `NRF_ROLE_RX`, `WL_ROLE_RX`, `CORE_CM4`, `LOG_BOARD_CPCU`

> **Never** define `USE_PWR_LDO_SUPPLY` without solder bridge rework. See §16.

---

## 13. Debug Logging

Both cores share USART3. HSEM #3 locks at message level (not per-character) for atomic lines.

```c
LOG(module, function, status, format, ...);    // structured: [CPCU - CM4]: func [OK] msg
LOG_CSV(format, ...);                          // raw CSV for SerialPlot
```

Modules: `CM4`, `CM7`, `IPC`, `NRF`, `PWM`, `TEST`. Status: `OK`, `FAIL`, `WARN`, `INFO`, `RUN`, `PASS`, `SKIP`.

---

## 14. Test Harness

### 14.1 Architecture

```
CM7: Init() → CM7_TestMain() → WirelessPacket tests → IPC tests → Monitor (CSV)
CM4: Init() → CM4_TestMain() → NRF_Test_All() → hybrid loop (real NRF + synth fallback)
```

### 14.2 CM7 Tests (unit + integration)

| ID    | Module           | Test                | Pass Criteria                  |
|-------|------------------|---------------------|--------------------------------|
| WL.1  | wireless_packet  | Roundtrip           | All fields match               |
| WL.2  | wireless_packet  | Max (4095)          | All decode to 4095             |
| WL.3  | wireless_packet  | Zero                | All decode to 0                |
| WL.4  | wireless_packet  | Alternating 0xAAA   | Pattern preserved              |
| IPC.1 | cpcu_ipc         | Control block       | magic + version correct        |
| IPC.2 | cpcu_ipc         | Push/pop single     | Data matches                   |
| IPC.3 | cpcu_ipc         | Overflow (129→127)  | ovf=2, first_seq=2             |
| IPC.4 | cpcu_ipc         | Batch (10 entries)  | count=10, seq 0-9              |
| IPC.5 | cpcu_ipc         | Motor command       | Fields roundtrip match         |

### 14.3 CM4 NRF Tests (Rev 6.0)

Run via `NRF_Test_All()` after `NRF_Init()` succeeds in `CM4_TestMain()`:

| Test          | What it catches                       |
|---------------|---------------------------------------|
| SPI Loopback  | Wiring faults, clock polarity, dead chip |
| Register Audit| Init-order bugs, write failures       |
| Address Verify| Multi-byte SPI transfer issues        |
| FIFO Exercise | FIFO controller faults                |
| RX Readiness  | CONFIG/CE wrong after init            |
| Power Cycle   | Register loss on PowerDown/PowerUp    |
| RX Live (3s)  | Actual packet reception from BSAU     |

### 14.4 CM4 Hybrid Test Loop (Rev 6.0)

After NRF tests, CM4 enters a loop that checks for real NRF packets first. If none arrive within `TEST_REAL_PKT_WINDOW_MS` (500 ms), falls back to synthetic generation at `TEST_SYNTH_INTERVAL_MS` (500 ms). This enables testing with or without BSAU connected.

### 14.5 SerialPlot CSV Format

Requires `CPCU_MODE_TEST_CSV`. Channels: `timestamp,seq,ch0-ch5,ring_count,overflows,gaps,state`.

---

## 15. Timing Constants (Rev 6.0)

All timeouts reviewed for robustness. Values are intentionally loose.

| Constant                  | Value  | File              | Rationale                           |
|---------------------------|--------|-------------------|-------------------------------------|
| `NRF_INIT_POR_DELAY_MS`  | 200 ms | cpcu_cm4_app.c    | NRF POR spec=100ms + 100% margin   |
| `NRF_INIT_MAX_RETRIES`   | 3      | cpcu_cm4_app.c    | Covers POR + SPI settling           |
| `NRF_INIT_BACKOFF_MS`    | 100 ms | cpcu_cm4_app.c    | Per-attempt × (attempt+1)           |
| `NRF_REINIT_INTERVAL_MS` | 3000 ms| cpcu_cm4_app.c    | Re-init period in DEGRADED          |
| `RADIO_TIMEOUT_MS`       | 750 ms | cpcu_cm4_app.c    | RUNNING→DEGRADED threshold          |
| `RECOVERY_PKT_COUNT`     | 10     | cpcu_cm4_app.c    | Consecutive OK before RUNNING       |
| `LINK_STATS_WINDOW_MS`   | 1000 ms| cpcu_cm4_app.c    | Link quality reporting period       |
| `IWDG1/2 timeout`        | 1000 ms| clock tree config | LSI/128, reload=1000                |
| `NRF_TEST_RX_TIMEOUT_MS` | 5000 ms| nrf24l01_test.h   | Wait for BSAU packet in RX test    |
| `TEST_SYNTH_INTERVAL_MS` | 500 ms | cpcu_test.h       | Synthetic packet rate (no BSAU)     |
| `TEST_REAL_PKT_WINDOW_MS`| 500 ms | cpcu_test.h       | Wait for real pkt before synth      |
| `CM7_DEBUG_INTERVAL_MS`  | 1000 ms| cpcu_cm7_app.c    | Diagnostic print period             |

---

## 16. Design Decisions

**CM4=radio+servos, CM7=DSP+ML:** CM4 (200 MHz) handles deterministic I/O; CM7 (400 MHz) handles compute-heavy DSP/ML with caches and FPU.

**Lock-free SPSC over RTOS:** Bare-metal, zero blocking. Only `__DMB()` costs a few cycles.

**SRAM4 for IPC:** D3 domain, powered in low-power modes, equal latency from both cores, non-cacheable via MPU.

**250 kbps over 2 Mbps:** Best sensitivity (−94 dBm), throughput needs only ~170 kbps.

**Message-level HSEM:** Per-character locking causes interleaving. Message-level makes each LOG line atomic.

**`cpcu_*_app.c` over `main.c`:** CubeMX owns `main.c`. 3 USER CODE lines survive any regeneration.

**400 MHz not 480 MHz:** Nucleo ships Supply Config 2 (Direct SMPS). VOS0 (480 MHz) needs LDO rework. Using `PWR_LDO_SUPPLY` without it bricks the board. **Recovery:** BOOT0 → VDD, CubeProgrammer → full erase.

---

## 17. Future Work

**CM7:** Goertzel frequency features, time-domain features (RMS/MAV/ZCR), windowing, X-CUBE-AI or TFLite-Micro classifier, 50 ms batch cycle with latency measurement.

**CM4:** Graceful servo ramp (not snap), flag-driven RELEASE behavior, frequency hopping, CRC integrity, servo feedback loop.

**BSAU:** ADC DMA at 2 kHz, full flags byte, battery voltage telemetry, low-power sleep.

**System:** FreeRTOS on CM7, DMA SPI on CM4, power management, dual-bank OTA, production self-test.

---

## 18. Conventions

**Code style:** C11, 4-space indent, `snake_case` functions, `UPPER_CASE` macros, `PascalCase` types. All custom code in `cpcu_*.c/h`.

**Memory safety:** No dynamic allocation. Shared memory via `volatile`. `__DMB()` on every cross-core write. MPU enforces non-cacheable SRAM4.

**Log format:** `[CPCU - MODULE]: function          [STATUS] detail`

**Version control:** Never commit `.ioc` without testing regeneration. Tag releases that change IPC layout. Keep `wireless_packet.h` in sync across repos.
