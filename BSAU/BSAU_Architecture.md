# BSAU Firmware Architecture

**Rev 2.2 — 2026-03-28 — bugrASl**  
**Target:** STM32L432KC (Cortex-M4, 80 MHz, 64 KB Flash, 16 KB SRAM)

---

## 1. Overview

6-channel EMG acquisition → 12-bit compression → nRF24L01+ radio TX at 250 kbps. CPU sleeps between DMA interrupts (~116 µA average). Packet rate: ~667 pkt/s (1.5 ms cycle).

**Signal chain:** Electrode → INA (×200) → DC offset (+1V) → Galvanic isolation (HCNR201) → BPF 10–500 Hz → Summing amp (0–3.3V) → ADC1 IN5–IN10. Battery via 100k/100k divider on PB0 (IN15).

---

## 2. Build Modes

Defined in `bsau_config.h` (included by `main.h`). Exactly one active — `log.h` enforces at compile time. Suffix `_LOG` = LOG on / CSV off. Suffix `_CSV` = LOG off / CSV on.

| Mode | LOG | CSV | Purpose |
|---|---|---|---|
| `RELEASE` | off | off | Production |
| `DEBUG` | on | on | Dev (structured logs + decimated CSV) |
| `TEST_ADC_CSV` | off | on | Binary ADC stream (SerialPlot) |
| `TEST_PKT_LOG` | on | off | WL codec round-trip verify |
| `TEST_CSV` | off | on | ASCII CSV capture |
| `TEST_DFT_LOG` | on | off | Goertzel frequency verify |
| `TEST_NRF_LOG` | on | off | NRF self-test + TX loop |

---

## 3. File Map

```
bsau_config.h       → Mode selection + LOG_BOARD_BSAU
main.h / main.c     → CubeMX-owned, calls BSAU_Init() + BSAU_Run()
bsau_app.h/c        → Public API, parameters, init/run dispatcher
bsau_adc.h/c        → ADC/DMA pipeline, snapshot, battery average
bsau_test.h/c       → All test mode implementations
log.h                → Unified LOG/LOG_CSV (shared with CPCU)
wireless_packet.h/c  → WL_Pack/WL_Unpack 12-bit codec
nrf24l01.h/c         → NRF24L01+ driver (TX role)
nrf24l01_test.h/c    → NRF self-test suite
```

---

## 4. Init → Run Flow

```
BSAU_Init():
  [test mode?] → BSAU_Test_Init() → return
  [production] → assert NVIC group → HAL_Delay(200ms POR) → NRF_Init(TX, retry×2) → BSAU_ADC_Init()

BSAU_Run():
  [test mode?] → BSAU_Test_Run() → return    ← has return, no fallthrough
  [production] → while(1) { WFI → build pkt → WL_Pack → NRF_Transmit }
```

**ADC pipeline:** TIM6 TRGO @ 2 kHz → 7-ch scan (4× HW oversample) → DMA circular (21 hw) → TC ISR copies snapshot, sets `g_pkt_ready`.

**ISR safety:** `HAL_ADC_ErrorCallback` stores error code in `g_adc_error_code` (volatile). Main loop logs it. Never call LOG from ISR — UART + HAL_GetTick deadlock risk at DMA priority 0 vs SysTick priority 15.

---

## 5. Packet Format (32 bytes)

```
[0]    seq       [1]    flags (bit0=BATT_LOW, bit1=SYS_ERR)
[2–28] 3 × 6 channels × 12-bit packed (pairs → 3 bytes each)
[29–31] reserved
```

Stride: `g_adc_snapshot[s * 7 + c]` — 7 is `ADC_DMA_CHANNELS`, not 6.

---

## 6. NRF Configuration

250 kbps, CH76, 0 dBm, CRC16, 5-byte address (E7×5), auto-ACK pipe 0, 1500 µs / 15 retries. TX poll timeout: 75 ms (3× worst case 24 ms). SPI @ 2 MHz, timeout 15 ms. Application-layer POR delay: 200 ms before first NRF_Init, with 1 retry + 100 ms backoff (per CPCU Rev 6.0 §6.1.7 compliance).

---

## 7. Test Modes

**PKT_LOG** — Runs once, then idles. Six sub-tests: channel ramp, boundary (0x000/0xFFF), seq wrap (0x00/0xFF), alternating (0xAAA/0x555), 13-bit overflow masking, raw byte layout verification. Reports pass/fail summary.

**NRF_LOG** — 200 ms POR delay + retry at init. Runs `NRF_Test_All()` (SPI, registers, address, FIFO, power cycle, TX) once. Then enters a phased runtime loop (150 ms interval): TX pings every iteration with PLOS/ARC tracking, SPI + register + FIFO health checks every 20 iterations (~3 s), power-cycle + address + TX stress tests every 100 iterations (~15 s). Aggregate summary every 50 iterations. Uses all `NRF_Test_*` functions from `nrf24l01_test.c`.

**ADC_CSV** — Binary frame: `[seq_lo][seq_hi][21 × uint16 LE]` = 44 bytes. Seq counter enables sync loss detection. SerialPlot: 22 channels uint16 LE (1 sync + 21 ADC).

**CSV** — ASCII: `seq,s0c0,...,s2c5,batt,drop\r\n`. Battery averaged across 3 scans. `drop` column = `g_adc_dropped` for pipeline health.

**DFT_LOG** — Goertzel 5-bin (50/100/200/350/500 Hz), block=512, fs=2000. Reports per-bin magnitude, dominant frequency, and concentration ratio (pseudo-SNR).

---

## 8. Timeout & Delay Summary

| Where | Value | Rationale |
|---|---|---|
| NRF POR delay | 200 ms | Datasheet §6.1.7: 100 ms max + 100% margin |
| NRF init retry backoff | 100 ms | 1 retry after POR if first attempt fails |
| LOG UART | 20 ms | 160 chars @ 115200 = 13.9 ms + 44% margin |
| LOG_CSV UART | 15 ms | 128 chars @ 115200 = 11.1 ms + 35% margin |
| ADC binary UART | 20 ms | 44 bytes @ 115200 = 3.8 ms |
| NRF SPI timeout | 15 ms | 33 bytes @ 2 MHz = 132 µs (safe margin) |
| NRF TX poll | 75 ms | Worst case 15 retries = 24 ms (3× margin) |
| NRF RX test wait | 5000 ms | BSAU boot + first TX (5 s) |
| NRF test TX interval | 150 ms | ~6.7 Hz repetitive TX rate |
| CE pulse delay | ~15 µs | volatile loop, >10 µs datasheet min |

---

## 9. Pin Map

```
PA0–5:  ADC IN5–10 (6× EMG)     PB0: ADC IN15 (battery)
PA6:    NRF_CSN (out)            PB3: SPI1_SCK (AF5)
PA7:    NRF_CE (out)             PB4: SPI1_MISO (AF5)
PA15:   STATUS_LED (out)         PB5: SPI1_MOSI (AF5)
PA9:    USART1_TX                PB6: NRF_IRQ (EXTI falling)
PA10:   USART1_RX
```

**Clocks:** MSI 4 MHz (no PLL), APB1/2 = 4 MHz, SPI = 2 MHz, TIM6 = 2 kHz TRGO. LSE 32.768 kHz for MSI auto-cal.

**NVIC:** DMA1_CH1 = priority 0 (highest), EXTI9_5 = 2, SysTick = 15 (lowest). Group 4 (4-bit preemption).
