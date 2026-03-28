# CPCU Test Procedure Guide

**Rev 1.0 — 2026-03-28 — bugrASl**

---

## 1. Hardware Setup

### 1.1 Minimum bench setup (TEST_LOG / TEST_CSV without BSAU)

```
                                        ST-LINK VCP (built-in)
┌──────────────────────────┐           ┌────────────┐
│  NUCLEO-H755ZI-Q         │    USB    │  PC / Term │
│                          │══════════→│  115200 8N1│
│  CM7 (400 MHz) ← IPC →  │           └────────────┘
│  CM4 (200 MHz)           │
│                          │
│  nRF24L01+ module        │  (8-pin header, see §1.3)
│  E-STOP button           │  PC13 (optional — Nucleo user button B1)
│                          │
│  USART3: PD8(TX) PD9(RX) │  ← routed to ST-LINK VCP automatically
│  SWD: PA13/PA14/PB3      │  ← ST-LINK built-in
└──────────────────────────┘
```

You need: NUCLEO-H755ZI-Q board, USB cable (powers board + provides VCP serial), a terminal at 115200 8N1 (PuTTY / minicom / CubeIDE serial monitor). The ST-LINK VCP is on USART3 (PD8/PD9) — no external USB-UART adapter needed.

### 1.2 Full setup (DEBUG / RELEASE with servos + BSAU)

```
                                                    ┌──────────┐
┌──────────────────────────┐                        │  BSAU    │
│  NUCLEO-H755ZI-Q         │     2.4 GHz RF link    │ L432KC   │
│                          │  ←  ←  ←  ←  ←  ←  ←  │ + NRF TX │
│  nRF24L01+ (RX)          │    CH76, E7×5 addr     │ + EMG×6  │
│                          │                        └──────────┘
│  Servo 0  ← PE9  (TIM1 CH1)  ──→  ┐
│  Servo 1  ← PE11 (TIM1 CH2)  ──→  │
│  Servo 2  ← PE13 (TIM1 CH3)  ──→  ├──→  6× hobby servos (5V supply!)
│  Servo 3  ← PE14 (TIM1 CH4)  ──→  │     50 Hz PWM, 500–2500 µs
│  Servo 4  ← PC6  (TIM8 CH1)  ──→  │
│  Servo 5  ← PC7  (TIM8 CH2)  ──→  ┘
│                          │
│  E-STOP   ← PC13 (user button B1 on Nucleo)
└──────────────────────────┘
```

**Servo power:** Do NOT power servos from the Nucleo 3.3V or 5V rail — they draw too much current. Use an external 5V / 2A supply with common GND to Nucleo.

### 1.3 NRF24L01+ wiring to NUCLEO-H755ZI-Q

```
  NRF24L01+ module          NUCLEO pin     Morpho connector
  ─────────────────         ──────────     ────────────────
  VCC  (pin 2)      →      3.3V           CN8 pin 7
  GND  (pin 1)      →      GND            CN8 pin 11
  CE   (pin 3)      →      PD15           CN10 pin 18
  CSN  (pin 4)      →      PD14           CN10 pin 16
  SCK  (pin 5)      →      PA5  (SPI1)    CN7 pin 10
  MOSI (pin 6)      →      PA7  (SPI1)    CN7 pin 14
  MISO (pin 7)      →      PA6  (SPI1)    CN7 pin 12
  IRQ  (pin 8)      →      PG6  (EXTI)    CN10 pin 30
```

**Critical notes:**
- NRF module must be 3.3V compatible (most breakout boards are). Never connect VCC to 5V.
- IRQ is active-low, open-drain. PG6 has an internal pull-up enabled in CubeMX.
- Add a 10 µF decoupling cap across VCC/GND right at the NRF module — the radio draws 13.5 mA TX bursts that cause voltage dips on long wires.
- Keep SPI wires short (< 10 cm). Longer wires at 1.5 MHz SCK can pick up noise.

### 1.4 Servo wiring

| Servo | Signal pin | Timer   | Channel | Nucleo morpho |
|-------|-----------|---------|---------|---------------|
| 0     | PE9       | TIM1    | CH1     | CN10 pin 4    |
| 1     | PE11      | TIM1    | CH2     | CN10 pin 6    |
| 2     | PE13      | TIM1    | CH3     | CN10 pin 10   |
| 3     | PE14      | TIM1    | CH4     | CN10 pin 12   |
| 4     | PC6       | TIM8    | CH1     | CN7 pin 1     |
| 5     | PC7       | TIM8    | CH2     | CN7 pin 11    |

PWM: 50 Hz, 1 count = 1 µs. Neutral = 1500 µs. Per-joint limits enforced by `SafetyClamp()`.

### 1.5 E-STOP

PC13 is the Nucleo user button (B1). It's active-high (press = rising edge → E-STOP fires). In the final prosthetic, wire a normally-closed emergency stop switch here. E-STOP is permanent — only a board reset clears it.

---

## 2. Mode-by-Mode Test Procedure

### Recommended test order

Test from the inside out — validate software layers before hardware integration:

```
1. TEST_LOG      software validation, NRF self-test (no BSAU needed)
2. TEST_LOG      + BSAU transmitting (validates RF link end-to-end)
3. TEST_CSV      SerialPlot visual validation (BSAU recommended)
4. DEBUG         full integration with servos
5. RELEASE       production acceptance
```

For all modes: change the active mode in `cpcu_config.h`, rebuild BOTH CM7 and CM4 sub-projects, flash via ST-LINK.

---

### 2.1 CPCU_MODE_TEST_LOG (phase 1: standalone)

**What it tests:** WL codec (CM7), IPC ring buffer (CM7+CM4), NRF hardware self-test (CM4), synthetic data pipeline.

**BSAU needed:** No.
**NRF module needed:** Yes (for NRF self-tests). Without NRF, init enters DEGRADED — tests are skipped but IPC tests still run.
**Servos needed:** No.
**Analog input needed:** No.
**UART:** Terminal at 115200 (ST-LINK VCP).

**Procedure:**
1. In `cpcu_config.h`, uncomment `CPCU_MODE_TEST_LOG`, comment all others.
2. Build CM7 and CM4. Flash both.
3. Open terminal. Output starts immediately.
4. Tests complete in ~30 seconds, then monitor captures 100 CSV lines.

**Expected output:**
```
[CPCU - CM7 ]: DWT_Init           [OK  ] Cycle counter enabled
[CPCU - CM7 ]: CPCU_CM7_Init      [RUN ] =================================================
[CPCU - CM7 ]: CPCU_CM7_Init      [INFO] InfiniTech CPCU — CM7 Online
[CPCU - CM7 ]: CPCU_CM7_Init      [INFO] SYSCLK=400 MHz  HCLK=200 MHz
[CPCU - CM7 ]: CPCU_CM7_Init      [INFO] BUILD   =   TEST
[CPCU - CM7 ]: CPCU_CM7_Init      [OK  ] IPC magic=0x494E4654 ver=0x0100 m7=1 m4=0
[CPCU - CM7 ]: CPCU_CM7_Init      [OK  ] =================================================
[CPCU - CM4 ]: CPCU_CM4_Init      [RUN ] =================================================
[CPCU - CM4 ]: CPCU_CM4_Init      [INFO] ======= InfiniTech CPCU — CM4 Online =======
[CPCU - CM4 ]: IPC_Handshake      [INFO] waiting for CM7...
[CPCU - CM4 ]: IPC_Handshake      [OK  ] CM7 ready, magic=0x494E4654
[CPCU - CM4 ]: IPC_Handshake      [OK  ] m4_ready=1, state=RUNNING (cold)
[CPCU - CM4 ]: NRF_Init           [INFO] ch=76 addr=E7:E7:E7:E7:E7 POR=200ms
[CPCU - CM4 ]: NRF_Init           [OK  ] radio up (attempt 1/3)
[CPCU - CM4 ]: LinkStats          [OK  ] window=1000ms expected=667pkt/s
[CPCU - CM4 ]: PWM_Start          [OK  ] 6ch at 1500us neutral
[CPCU - CM4 ]: CPCU_CM4_Init      [OK  ] =================================================
[CPCU - CM4 ]: CPCU_CM4_Init      [INFO] radio=OK nrf=0 state=1 timeout=750ms
[CPCU - TEST]: CM7_TestMain       [RUN ] ================ CPCU TEST SUITE START =================
[CPCU - TEST]: WirelessPacket     [RUN ] starting codec tests
[CPCU - TEST]: WL_Roundtrip       [PASS] seq=42
[CPCU - TEST]: WL_Roundtrip       [PASS] flags=BATT_LOW
[CPCU - TEST]: WL_Roundtrip       [PASS] all sample data matches
[CPCU - TEST]: WL_Extremes        [PASS] all channels = 4095
[CPCU - TEST]: WL_Extremes        [PASS] all channels = 0
[CPCU - TEST]: WL_Alternating     [PASS] 0xAAA/0x555 pattern intact
[CPCU - TEST]: WL_FlagsAll        [PASS] flags=0xFF (expect 0xFF)
[CPCU - TEST]: WirelessPacket     [PASS] all codec tests passed
[CPCU - TEST]: CM4_TestMain       [RUN ] CM4 test mode active
[CPCU - TEST]: CM4_TestMain       [RUN ] running NRF self-test suite...
[CPCU - NRF ]: Test_All           [RUN ] === NRF24L01+ SELF-TEST START ===
[CPCU - NRF ]: SPI_Loopback       [PASS] wrote=0x02, read=0x02
[CPCU - NRF ]: SPI_Restore        [PASS] restored=0x03, expected=0x03
[CPCU - NRF ]: Reg_Check          [PASS] SETUP_AW:    got=0x03, expected=0x03, mask=0x03
[CPCU - NRF ]: Reg_Check          [PASS] RF_CH:       got=0x4C, expected=0x4C, mask=0x7F
[CPCU - NRF ]: Reg_Check          [PASS] RF_SETUP:    got=0x26, expected=0x26, mask=0x2E
[CPCU - NRF ]: Reg_Check          [PASS] EN_AA:       got=0x01, expected=0x01, mask=0x3F
[CPCU - NRF ]: Reg_Check          [PASS] EN_RXADDR:   got=0x01, expected=0x01, mask=0x3F
[CPCU - NRF ]: Reg_Check          [PASS] RX_PW_P0:    got=0x20, expected=0x20, mask=0x3F
[CPCU - NRF ]: Reg_Check          [PASS] SETUP_RETR:  got=0x5F, expected=0x5F, mask=0xFF
[CPCU - NRF ]: Reg_Check          [PASS] CONFIG:      got=0x3F, expected=0x3F, mask=0x7F
[CPCU - NRF ]: Addr_RX_P0         [PASS] got=E7:E7:E7:E7:E7 exp=E7:E7:E7:E7:E7
[CPCU - NRF ]: FIFO_InitEmpty     [PASS] TX_EMPTY=1 (expect 1)
[CPCU - NRF ]: FIFO_RxEmpty       [PASS] RX_EMPTY=1 (expect 1)
[CPCU - NRF ]: FIFO_NonEmpty      [PASS] TX_EMPTY=0 (Post-Write expect 0)
[CPCU - NRF ]: FIFO_Flushed       [PASS] TX_EMPTY=1 after flush (expect 1)
[CPCU - NRF ]: RX_PwrUp           [PASS] PWR_UP=1 (expect 1)
[CPCU - NRF ]: RX_PrimRx          [PASS] PRIM_RX=1 (expect 1)
[CPCU - NRF ]: RX_CE              [PASS] CE=1 (expect 1)
[CPCU - NRF ]: Test_PwrCycle      [RUN ] PowerDown → PowerUp → verify CONFIG
[CPCU - NRF ]: PwrCycle_PwrUp     [PASS] PWR_UP=1 (expect 1)
[CPCU - NRF ]: PwrCycle_Role      [PASS] PRIM_RX before=1 after=1
[CPCU - NRF ]: PwrCycle_Channel   [PASS] RF_CH before=76 after=76
[CPCU - NRF ]: Test_All           [PASS] === NRF SELF-TEST: 7 PASS, 0 FAIL ===
[CPCU - TEST]: CM4_TestMain       [PASS] NRF self-test passed
[CPCU - TEST]: CM4_TestMain       [RUN ] entering hybrid loop (real=500ms synth=500ms)
[CPCU - TEST]: IPC                [RUN ] starting IPC tests
[CPCU - TEST]: IPC_Magic          [PASS] magic=0x494E4654
[CPCU - TEST]: IPC_Version        [PASS] version=0x0100
[CPCU - TEST]: IPC_M7Ready        [PASS] m7_ready=1
[CPCU - TEST]: IPC_PushPop        [PASS] pop returned true
[CPCU - TEST]: IPC_PushPop        [PASS] seq=99 (expected 99)
[CPCU - TEST]: IPC_PushPop        [PASS] ch0=1234 (expected 1234)
[CPCU - TEST]: IPC_PushPop        [PASS] ch1=5678 (expected 5678)
[CPCU - TEST]: IPC_Overflow       [PASS] count=127 (max=128)
[CPCU - TEST]: IPC_Overflow       [PASS] ovf before=0 after=2
[CPCU - TEST]: IPC_Overflow       [PASS] first_seq=2 (expected 2)
[CPCU - TEST]: IPC_Batch          [PASS] popped=10 (expected 10)
[CPCU - TEST]: IPC_Batch          [PASS] first=0 last=9
[CPCU - TEST]: IPC_MotorCmd       [PASS] read returned true
[CPCU - TEST]: IPC_MotorCmd       [PASS] s0=1500 s1=1200
[CPCU - TEST]: IPC_MotorCmd       [PASS] g=3 c=95
[CPCU - TEST]: IPC_EmptyPop       [PASS] pop on empty ring returned false
[CPCU - TEST]: IPC                [PASS] all IPC tests passed
[CPCU - TEST]: CM7_TestMain       [INFO] CM4 NRF init status=0
[CPCU - TEST]: CM7_TestMain       [INFO] === RESULTS: 2 PASS, 0 FAIL, 0 SKIP ===
[CPCU - TEST]: CM7_TestMain       [INFO] entering live monitor...
[CPCU - TEST]: CM4_Synth          [INFO] no real pkts for 500ms — synthetic mode
```

Then 100 CSV lines of synthetic sawtooth data, then idle.

**Pass criteria:** All lines show `[PASS]`. Init summary shows `radio=OK nrf=0 state=1`. NRF self-test: `7 PASS, 0 FAIL`. IPC tests: `2 PASS, 0 FAIL, 0 SKIP`. CM4 falls back to synthetic after 500 ms (expected without BSAU).

---

### 2.2 CPCU_MODE_TEST_LOG (phase 2: with BSAU)

**Same build as phase 1.** Just power on BSAU (in `BSAU_MODE_DEBUG` or `BSAU_MODE_TEST_NRF_LOG`).

**What changes:** CM4 hybrid loop detects real NRF packets and you see `CM4_RealPkt` lines instead of synthetic fallback.

**Additional expected output (replaces synthetic lines):**
```
[CPCU - TEST]: CM4_RealPkt        [OK  ] seq=0 ch0=2048 flags=0x80
[CPCU - TEST]: CM4_RealPkt        [OK  ] seq=1 ch0=2047 flags=0x00
[CPCU - TEST]: CM4_RealPkt        [OK  ] seq=2 ch0=2051 flags=0x00
...
```

**What to look for:**
- `flags=0x80` on the very first packet = FIRST_PACKET flag (BSAU boot sync) — correct.
- `flags=0x00` on subsequent packets — correct.
- `seq` incrementing without gaps — good RF link.
- `ch0` values in the 2000–2100 range (floating EMG inputs at mid-rail) — correct.
- If you see `SeqGap WARN` lines, check antenna distance / orientation.

---

### 2.3 CPCU_MODE_TEST_CSV

**What it tests:** End-to-end data pipeline with clean CSV output for SerialPlot. No LOG noise.

**BSAU needed:** Recommended (real waveforms). Without BSAU, you get synthetic sawtooth.
**NRF module needed:** Yes.
**Servos needed:** No.
**UART:** SerialPlot (not a text terminal).

**Procedure:**
1. In `cpcu_config.h`, uncomment `CPCU_MODE_TEST_CSV`.
2. Build CM7 + CM4. Flash both.
3. Open SerialPlot:
   - Port: ST-LINK VCP COM port
   - Baud: 115200
   - Data format: ASCII
   - Delimiter: Comma
   - Number of channels: 12
   - Filter: Exclude prefix `#`

**SerialPlot channel mapping:**

| Channel | Data                       | Expected range | What to watch |
|---------|----------------------------|----------------|---------------|
| 0       | Timestamp (ms)             | 0 → ∞         | Monotonic increase |
| 1       | Packet sequence            | 0 – 255        | Sawtooth (wraps at 255) |
| 2–7     | EMG channels 0–5 (12-bit)  | 0 – 4095       | Waveforms (real) or sawtooth (synth) |
| 8       | Ring buffer occupancy      | 0 – 128        | Should hover near 0 |
| 9       | Ring overflow count        | 0 → ∞         | Should stay flat at 0 |
| 10      | Sequence gap count         | 0 → ∞         | Should stay flat at 0 |
| 11      | System state               | 0, 1, 2        | Steady 1 (RUNNING) |

**Pass criteria:**
- Channels 2–7: live EMG waveforms (with BSAU) or clean sawtooth ramps (without BSAU).
- Channel 8: near 0 (ring not backing up → CM7 keeping pace).
- Channel 9: flat 0 (no overflows → CM4 not outrunning CM7).
- Channel 10: flat 0 (no packet loss → clean RF link).
- Channel 11: steady 1 (RUNNING, not SAFE).
- No `#` comment lines (LOG is disabled in this mode — only CSV data and periodic `#` diagnostics).

**If channel 11 drops to 2 (SAFE):** Radio link died. Check NRF wiring, BSAU power, antenna.

---

### 2.4 CPCU_MODE_DEBUG

**What it tests:** Complete production path with full instrumentation. Threshold classifier on CM7, servo actuation on CM4, both LOG and LOG_CSV active.

**BSAU needed:** Yes (required for real motor commands — without BSAU, no sensor data → no classification → no servo movement).
**NRF module needed:** Yes.
**Servos needed:** Recommended (to see physical movement). Can test without — just read motor command LOG lines.
**Analog input (on BSAU side):** EMG electrodes or function generator on BSAU ADC inputs.
**UART:** Terminal at 115200 (mixed LOG + CSV output).

**Procedure:**
1. In `cpcu_config.h`, uncomment `CPCU_MODE_DEBUG`.
2. Build CM7 + CM4. Flash both.
3. Power on BSAU.
4. Open terminal.

**Expected output (init):**
```
[CPCU - CM7 ]: CPCU_CM7_Init      [INFO] InfiniTech CPCU — CM7 Online
[CPCU - CM7 ]: CPCU_CM7_Init      [INFO] SYSCLK=400 MHz  HCLK=200 MHz
[CPCU - CM7 ]: CPCU_CM7_Init      [INFO] BUILD   =   DEBUG
[CPCU - CM7 ]: CPCU_CM7_Init      [INFO] Threshold low=800 mid=1600 high=2800
[CPCU - CM4 ]: NRF_Init           [OK  ] radio up (attempt 1/3)
[CPCU - CM4 ]: PWM_Start          [OK  ] 6ch at 1500us neutral
[CPCU - CM4 ]: CPCU_CM4_Init      [INFO] radio=OK nrf=0 state=1 timeout=750ms
```

**Expected output (runtime, with BSAU transmitting):**
```
[CPCU - NRF ]: RxPacket           [OK  ] seq=42 f=0x00 ch0-2=2048,2051,2044
[CPCU - CM7 ]: PopSensorBatch     [OK  ] count=3
[CPCU - CM7 ]: MotorCmd           [OK  ] zone=0 conf=0% s=[1500,1500,1500,1500,1500,1500]
1234567,42,2048,2051,2044,2049,2046,2050,0,0
[CPCU - CM4 ]: MotorCmd           [OK  ] g=0 c=0% s=[1500,1500,1500,1500,1500,1500]
```

When you flex a muscle (if EMG connected):
```
[CPCU - CM7 ]: MotorCmd           [OK  ] zone=2 conf=66% s=[2166,2166,1500,1500,1500,1500]
[CPCU - CM4 ]: MotorCmd           [OK  ] g=2 c=66% s=[2166,2166,1500,1500,1500,1500]
```

**Link quality (every 1 second):**
```
[CPCU - NRF ]: LinkStats          [INFO] loss=0.0% rate=665/s jit=1.5ms pk=2ms rpd=1 lq=100
```

**CM7 diagnostics (every 1 second):**
```
[CPCU - CM7 ]: Diagnostics        [INFO] t=5s state=1 pkts=3325 gaps=0 ovf=0 batches=1108 m4=1 nrf=0
```

**Pass criteria:**
- `zone=0 conf=0%` at rest (no muscle activity) — servos at 1500 µs neutral.
- `zone=1/2/3` with increasing confidence when you flex — servos move toward 2500 µs.
- No `SafetyClamp WARN` lines (classifier output within per-joint limits).
- `loss=0.0%` in LinkStats (clean RF link).
- `lq=100` or close (composite link quality).
- `ovf=0` (CM7 processing fast enough).
- `gaps=0` (no RF packet loss).
- If servos connected: physical movement tracks flex/relax. Neutral on relax.

---

### 2.5 CPCU_MODE_RELEASE

**What it tests:** Nothing visible — this is production firmware. Both LOG and LOG_CSV compile to `((void)0)`. Zero UART output. Minimum flash size, maximum performance.

**BSAU needed:** Yes.
**NRF module needed:** Yes.
**Servos needed:** Yes (the only way to verify function is physical observation or oscilloscope).
**UART:** Silent. If you see any output, the build is wrong.

**Procedure:**
1. In `cpcu_config.h`, uncomment `CPCU_MODE_RELEASE`.
2. Build CM7 + CM4. Flash both.
3. Power on BSAU with EMG electrodes attached.
4. **Verify silence:** Terminal shows absolutely nothing. If it does, wrong mode.
5. **Verify function:** Flex muscle → servos move. Relax → servos return to neutral.
6. **Verify E-STOP:** Press Nucleo user button (B1 / PC13). All servos snap to 1500 µs and stay there. No recovery without board reset.
7. **Verify watchdog:** If you have a debugger attached, verify `uwTick` is incrementing on both cores. IWDG1/IWDG2 should never fire during normal operation.
8. **Verify power cycle:** Unplug USB, replug. System should return to RUNNING within 1 second (NRF POR delay + IPC handshake).

**Oscilloscope checks (if available):**
- PE9 (TIM1 CH1, Servo 0): 50 Hz PWM, pulse width 1500 µs at rest, 1833–2500 µs on flex.
- Verify all 6 servo pins show correct 50 Hz waveform.
- Verify E-STOP forces all 6 channels to exactly 1500 µs.

---

## 3. Quick Reference Matrix

| Mode | NRF HW | BSAU | Servos | UART output | What you're testing |
|---|---|---|---|---|---|
| `TEST_LOG` | Yes* | No** | No | Terminal (LOG) | Software + NRF HW + IPC |
| `TEST_CSV` | Yes | Recommended | No | SerialPlot (CSV) | Data pipeline visual |
| `DEBUG` | Yes | Yes | Recommended | Terminal (LOG+CSV) | Full integration |
| `RELEASE` | Yes | Yes | Yes | None (silent) | Production acceptance |

*Without NRF module: enters DEGRADED, NRF tests skipped, IPC + WL tests still run.
**With BSAU: hybrid loop shows real packets instead of synthetic.

---

## 4. Pin Connection Checklist

Use this checklist before each test session:

### Always required (all modes)

| Connection | Pin | Check |
|---|---|---|
| USB cable to Nucleo | CN1 | Powers board + ST-LINK VCP |
| ST-LINK VCP enumerated | — | COM port visible in Device Manager |

### Required for TEST_LOG, TEST_CSV, DEBUG, RELEASE

| Connection | Pin | Check |
|---|---|---|
| NRF VCC → 3.3V | CN8 pin 7 | Measure 3.3V at NRF module |
| NRF GND → GND | CN8 pin 11 | Common ground |
| NRF CE → PD15 | CN10 pin 18 | Continuity |
| NRF CSN → PD14 | CN10 pin 16 | Continuity |
| NRF SCK → PA5 | CN7 pin 10 | Continuity |
| NRF MOSI → PA7 | CN7 pin 14 | Continuity |
| NRF MISO → PA6 | CN7 pin 12 | Continuity |
| NRF IRQ → PG6 | CN10 pin 30 | Continuity |
| 10 µF cap across NRF VCC/GND | — | Decoupling for TX bursts |

### Required for DEBUG and RELEASE (with servos)

| Connection | Pin | Check |
|---|---|---|
| Servo 0 signal → PE9 | CN10 pin 4 | |
| Servo 1 signal → PE11 | CN10 pin 6 | |
| Servo 2 signal → PE13 | CN10 pin 10 | |
| Servo 3 signal → PE14 | CN10 pin 12 | |
| Servo 4 signal → PC6 | CN7 pin 1 | |
| Servo 5 signal → PC7 | CN7 pin 11 | |
| Servo VCC → external 5V | — | NOT from Nucleo! |
| Servo GND → common GND | — | Shared with Nucleo GND |

### Optional (E-STOP testing)

| Connection | Pin | Check |
|---|---|---|
| Nucleo B1 button | PC13 | Built-in, rising edge trigger |
| External E-STOP switch | PC13 | Normally-closed, active-high |

---

## 5. Common Failure Scenarios

**"NRF_Init FAIL ret=2 — entered DEGRADED"**
→ NRF not responding to SPI. Check wiring per §1.3. Verify 3.3V at NRF VCC pin with multimeter. Check that decoupling cap is present. In DEGRADED, the system retries every 3 s — watch for `NRF_ReInit OK` which means it recovered after a slow power-up.

**"state=2 (SAFE) immediately at boot"**
→ Old firmware without Rev 6.0 NRF retry fix. Reflash with updated code. Or: E-STOP pin (PC13) is being held high at boot (button pressed, or floating with no pull-down).

**"All NRF tests PASS but no packets from BSAU"**
→ BSAU and CPCU on different RF channels (both must be 76), different addresses (both must be E7×5), or different data rates (both must be 250 kbps). Also check: BSAU is actually transmitting (verify BSAU terminal shows TX activity). Physical distance > 10 m at 0 dBm with PCB antennas can cause 100% loss.

**"Packets arriving but zone always 0, servos don't move"**
→ EMG channels reading near zero (electrodes not connected, or BSAU ADC not configured). Check BSAU CSV output to verify ADC values. Threshold classifier needs channels > 800 to register zone 1.

**"SafetyClamp WARN on every motor command"**
→ Classifier generating out-of-range servo values. Check threshold constants in `cpcu_cm7_app.h`. The `ZoneToServo()` function should produce values between 1500 and 2500.

**"Ring overflow count incrementing"**
→ CM7 not consuming sensor data fast enough. In DEBUG mode, the LOG output itself slows down CM7 (HSEM + printf). Reduce LOG verbosity or switch to TEST_CSV mode. In RELEASE mode this should never happen.

**"LinkStats shows loss > 5%"**
→ RF interference or distance issue. Try: shorter antenna distance, different channel (change `NRF_RF_CHANNEL` in `cpcu_cm4_app.h` and matching BSAU config), orient antennas perpendicular to each other. The 250 kbps mode has −94 dBm sensitivity — if RPD shows 0, the signal is below −64 dBm (weak but possibly still decodable).

**"IWDG2 warm recovery detected" at boot**
→ CM4 watchdog fired on the previous run. This means CM4 was stuck for > 1 second. Common causes: NRF SPI timeout with bad wiring, infinite loop in a flag processing path, or LOG printf blocking for too long (if HSEM is never released by CM7). The warm recovery should succeed — check that IPC magic is valid and CM4 resumes.

**Terminal shows garbled text**
→ Baud rate mismatch (must be 115200), or both cores printing simultaneously without HSEM protection. Verify `LOG_BOARD_CPCU` is defined (enables HSEM locking). If using a mode where only one macro is active (TEST_LOG or TEST_CSV), interleaving should not occur.
