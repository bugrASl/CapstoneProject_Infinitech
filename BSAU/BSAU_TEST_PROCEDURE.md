# BSAU Test Procedure Guide

**Rev 1.0 — 2026-03-28 — bugrASl**

---

## 1. Hardware Setup

### Minimum bench setup (all modes except NRF link test)

```
┌─────────────┐         USB-UART          ┌────────────┐
│  BSAU PCB   │──── PA9 (TX) ───────────→ │  PC / Term │
│ STM32L432KC │──── PA10 (RX) ←───────── │  115200 8N1│
│             │                           └────────────┘
│  nRF24L01+  │  (on-board, SPI1)
│  6× EMG in  │  PA0–PA5 (can be left floating for digital-only tests)
│  Batt div   │  PB0
│  LED        │  PA15
└─────────────┘
```

You need: BSAU board, ST-LINK (SWD for flashing), USB-UART adapter on PA9/PA10, a terminal (PuTTY / minicom / CubeIDE serial monitor) at 115200 8N1.

### Full setup (for NRF link test with ACK)

Add a CPCU board with its nRF24L01+ in RX mode on the same channel (76) and address (E7×5). Without CPCU, NRF TX tests will get `MAX_RT` (15 retries exhausted, no ACK) — this is still a **PASS** for the BSAU side since it proves the RF state machine is working. The only difference is whether you see `TX_DS` (ACK received) or `MAX_RT` (no receiver).

### For analog chain tests (ADC_CSV, CSV, DFT_LOG)

Connect a signal source to at least one EMG input (PA0–PA5). Options: function generator (sine, 10–500 Hz, 0–3.3V), or just touch the electrode leads with your finger for a live noisy EMG signal. For DFT verification, a clean sine at a known frequency (e.g. 100 Hz, 1 Vpp, 1.65V DC offset) is ideal.

---

## 2. Mode-by-Mode Test Procedure

### Recommended test order

Test from the inside out — validate the lower layers before testing the higher ones:

```
1. PKT_LOG      pure software, no hardware dependency
2. NRF_LOG      validates SPI + radio hardware
3. ADC_CSV      validates ADC + DMA pipeline (binary)
4. CSV           validates ADC + DMA pipeline (ASCII, readable)
5. DFT_LOG       validates analog front-end frequency response
6. DEBUG         validates full production path
7. RELEASE       final production build
```

---

### 2.1 BSAU_MODE_TEST_PKT_LOG

**What it tests:** WL_Pack / WL_Unpack 12-bit compression codec in pure software. No hardware dependencies beyond UART for log output.

**CPCU needed:** No.  
**Analog input needed:** No.  
**NRF module needed:** No (not initialized in this mode).

**Procedure:**
1. In `bsau_config.h`, uncomment `BSAU_MODE_TEST_PKT_LOG`, comment all others.
2. Build and flash.
3. Open terminal at 115200.
4. Tests run once automatically at boot, then LED blinks at 1 Hz idle.

**Expected output:**
```
[BSAU - TEST]: BSAU_Test_Init    [RUN ]
[BSAU - TEST]: BSAU_Test_Init    [OK  ] Packet codec verify mode ready
[BSAU - TEST]: PacketVerify       [RUN ] === WL CODEC ROUND-TRIP START ===
[BSAU - TEST]: PKT_Ramp          [RUN ] ramp 0x100..0x600, seq=0x55, flags=0x03
[BSAU - TEST]: PKT_Seq           [PASS] got=0x55 exp=0x55
[BSAU - TEST]: PKT_Flags         [PASS] got=0x03 exp=0x03
[BSAU - TEST]: PKT_Samples       [PASS] all 18 samples match
[BSAU - TEST]: PKT_Rsvd          [PASS] reserved bytes match
...
[BSAU - TEST]: PKT_Ovf_0         [PASS] 0x1ABC→0xABC exp=0xABC
[BSAU - TEST]: PKT_Raw_Seq       [PASS] raw[0]=0x42 exp=0x42
...
[BSAU - TEST]: PacketVerify       [PASS] === WL CODEC: N PASS, 0 FAIL ===
[BSAU - TEST]: BSAU_Test_Run     [INFO] Tests complete. Idling.
```

**Pass criteria:** All sub-tests show `[PASS]`, final summary shows 0 FAIL.

---

### 2.2 BSAU_MODE_TEST_NRF_LOG

**What it tests:** SPI connectivity, NRF24L01+ register configuration, FIFO controller, power cycle, TX RF state machine. Runtime loop periodically re-validates SPI, registers, FIFO, and power cycle to catch intermittent faults.

**CPCU needed:** No (MAX_RT is an expected PASS without a receiver). Optional: connect CPCU in RX mode to see TX_DS instead of MAX_RT.  
**Analog input needed:** No (ADC pipeline is not started).  
**NRF module needed:** Yes — must be physically connected on SPI1 (PB3–5) with CSN (PA6), CE (PA7), IRQ (PB6).

**Procedure:**
1. In `bsau_config.h`, uncomment `BSAU_MODE_TEST_NRF_LOG`.
2. Build and flash.
3. Open terminal.

**Expected output (init):**
```
[BSAU - TEST]: BSAU_Test_Init    [RUN ]
[BSAU - TEST]: NRF_Test_Init     [RUN ] POR wait done, initializing CH76
[BSAU - TEST]: NRF_Test_Init     [OK  ] Radio ready on CH76
[BSAU - NRF ]: Test_All          [RUN ] === NRF24L01+ SELF-TEST START ===
[BSAU - NRF ]: SPI_Loopback      [PASS] wrote=0x02, read=0x02
[BSAU - NRF ]: SPI_Restore       [PASS] restored=0x03, expected=0x03
[BSAU - NRF ]: Reg_Check         [PASS] SETUP_AW: got=0x03, expected=0x03, mask=0x03
...
[BSAU - NRF ]: PwrDown           [PASS] PWR_UP=0 after PowerDown (expect 0)
[BSAU - NRF ]: PwrDown_SPI       [PASS] RF_CH=0x4C while powered down (expect 0x4C)
[BSAU - NRF ]: PwrUp             [PASS] PWR_UP=1 after PowerUp (expect 1)
[BSAU - NRF ]: PwrRestore        [PASS] CONFIG=0x4E after cycle (expect 0x4E)
[BSAU - NRF ]: TX_Result         [PASS] MAX_RT=1 — no receiver, state machine OK (PLOS=1 ARC=15)
[BSAU - NRF ]: Test_All          [PASS] === NRF SELF-TEST: 6 PASS, 0 FAIL ===
```

**Expected output (runtime, repeating):**
```
[BSAU - TEST]: NRF_TxPing        [PASS] iter=1 PLOS=0 ARC=15
[BSAU - TEST]: NRF_TxPing        [PASS] iter=2 PLOS=0 ARC=15
...
[BSAU - TEST]: NRF_Health        [RUN ] periodic SPI/reg/FIFO check at iter=20
[BSAU - NRF ]: SPI_Loopback      [PASS] ...
[BSAU - NRF ]: Reg_Check         [PASS] ...
[BSAU - TEST]: NRF_Health        [PASS] SPI+reg+FIFO OK (pass=1)
...
[BSAU - TEST]: NRF_Summary       [INFO] tx_ok=48 tx_fail=0 rate=100% health=2/2 t=7s
...
[BSAU - TEST]: NRF_Stress        [RUN ] power cycle + address + TX at iter=100
[BSAU - NRF ]: PwrDown           [PASS] ...
[BSAU - NRF ]: PwrUp             [PASS] ...
[BSAU - TEST]: NRF_Stress        [PASS] power cycle + addr + TX OK
```

**Pass criteria:** `NRF_Test_All` shows 0 FAIL. TX pings show `PASS` (TX_DS or MAX_RT both count). Health and stress checks show `PASS`. If CPCU is connected and receiving, you'll see `TX_DS=1` instead of `MAX_RT=1`.

**Failure interpretation:**
- `NRF_ERR_NOT_DETECTED` at init → check SPI wiring, NRF power supply, solder joints.
- Health check `FAIL` after running fine → EMI issue, loose connection, or thermal problem.
- `TX_TIMEOUT` → NRF state machine stuck. Check CE/CSN wiring, crystal, VDD.

---

### 2.3 BSAU_MODE_TEST_ADC_CSV

**What it tests:** ADC + DMA pipeline in binary mode. Verifies TIM6 triggering, DMA circular transfers, and data integrity. Output is raw binary — not human-readable on a terminal.

**CPCU needed:** No.  
**Analog input needed:** Optional (floating inputs produce noise, which is fine for pipeline testing).  
**NRF module needed:** No (not initialized).

**Procedure:**
1. In `bsau_config.h`, uncomment `BSAU_MODE_TEST_ADC_CSV`.
2. Build and flash.
3. Open **SerialPlot** (not a text terminal).
4. Configure: 22 channels, uint16 little-endian, 115200 baud.
   - Channel 0 = sync counter (should increment by 1 each frame).
   - Channels 1–21 = ADC values (3 scans × 7 channels).

**Pass criteria:** Sync counter increments monotonically without gaps. ADC channels show stable noise floor (~2040–2060 for floating inputs at 1.65V mid-rail) or the injected signal waveform. LED toggles at DMA rate (~667 Hz). No frame drops (no counter gaps).

**LOG output:** None (`LOG_ENABLED = 0` in this mode). Only binary data on UART.

---

### 2.4 BSAU_MODE_TEST_CSV

**What it tests:** Same ADC + DMA pipeline, but as human-readable ASCII CSV. Easier to inspect than binary, but slower (longer UART frames).

**CPCU needed:** No.  
**Analog input needed:** Optional (same as ADC_CSV).  
**NRF module needed:** No.

**Procedure:**
1. In `bsau_config.h`, uncomment `BSAU_MODE_TEST_CSV`.
2. Build and flash.
3. Open terminal at 115200, or SerialPlot in ASCII CSV mode.

**Expected output:**
```
seq,s0c0,s0c1,s0c2,s0c3,s0c4,s0c5,s1c0,...,s2c5,batt,drop
0,2048,2047,2050,2044,2051,2049,...,2045,1862,0
1,2049,2046,2051,2043,2050,2048,...,2046,1861,0
...
```

**Pass criteria:** `seq` increments without gaps. `batt` is the averaged battery ADC value (should be stable, ~1860 at 3.0V with 100k/100k divider). `drop` stays at 0 (if it increments, the CPU isn't consuming packets fast enough — likely a UART bandwidth issue). ADC values are in the expected range for your input.

---

### 2.5 BSAU_MODE_TEST_DFT_LOG

**What it tests:** End-to-end analog front-end frequency response. Feeds live ADC samples (channel 0 only) into a Goertzel filter bank and reports magnitude at 50, 100, 200, 350, 500 Hz.

**CPCU needed:** No.  
**Analog input needed:** **Yes** — a signal generator on PA0 (ADC1_IN5). Sine wave, known frequency (e.g. 100 Hz), amplitude 0–3.3V, DC offset ~1.65V.  
**NRF module needed:** No.

**Procedure:**
1. In `bsau_config.h`, uncomment `BSAU_MODE_TEST_DFT_LOG`.
2. Connect function generator to PA0.
3. Build and flash.
4. Open terminal.

**Expected output (with 100 Hz sine input):**
```
[BSAU - TEST]: BSAU_Test_Init    [OK  ] DFT configured: 5 bins, block=512, fs=2000
...
[BSAU - TEST]: DFT_Verify        [INFO] blk=0 | 50Hz=12 100Hz=8432 200Hz=8 350Hz=3 500Hz=2 | peak=100Hz conc=99%
[BSAU - TEST]: DFT_Verify        [INFO] blk=1 | 50Hz=10 100Hz=8501 200Hz=6 350Hz=4 500Hz=1 | peak=100Hz conc=99%
```

**Pass criteria:** The bin matching your input frequency dominates (high magnitude). Concentration > 90% for a clean sine. Other bins should be near zero. If you see 50 Hz dominant when no 50 Hz signal is applied, you have mains hum coupling — check your isolation stage.

**Verification matrix:**

| Input | Expected peak | Purpose |
|---|---|---|
| 100 Hz sine | 100Hz bin dominates | Confirm passband center |
| 50 Hz sine | 50Hz bin dominates | Confirm HPF cutoff not too high |
| 500 Hz sine | 500Hz bin dominates | Confirm LPF cutoff not too low |
| No input (floating) | No dominant bin, low magnitudes | Confirm noise floor |
| 1 kHz sine | All bins low | Confirm anti-alias LPF is working |

---

### 2.6 BSAU_MODE_DEBUG

**What it tests:** The complete production path — ADC pipeline, packet assembly, NRF transmit — with both LOG and LOG_CSV active. This is your normal development mode.

**CPCU needed:** Optional. Without CPCU, you get `MAX_RT` on every TX (NRF retries, then fails — adds ~24 ms latency per packet). With CPCU receiving, you get `TX_DS` and proper throughput.  
**Analog input needed:** Optional (floating inputs produce noise-floor data — valid for pipeline testing).  
**NRF module needed:** Yes.

**Procedure:**
1. In `bsau_config.h`, uncomment `BSAU_MODE_DEBUG`.
2. Build and flash.
3. Open terminal.

**Expected output:**
```
[BSAU - APP ]: BSAU_Init         [RUN ]
[BSAU - APP ]: BSAU_Init         [OK  ] NVIC priority group verified
[BSAU - NRF ]: NRF_Init          [RUN ] ch=76 (POR wait 200ms done)
[BSAU - NRF ]: NRF_Init          [OK  ]
[BSAU - ADC ]: BSAU_ADC_Init     [RUN ] Calibrating ADC1...
[BSAU - ADC ]: BSAU_ADC_Init     [OK  ] Pipeline running (TIM6 trig, DMA circ)
[BSAU - APP ]: BSAU_Init         [OK  ] pipeline live
[BSAU - APP ]: BSAU_Run          [RUN ] entering main loop
```

Then every ~0.5 s (every 329 packets):
```
[BSAU - APP ]: BSAU_Run          [INFO] seq=73 batt=1862 flag=OK ok=329 lost=0 drop=0
```

And every ~0.1 s (every 67 packets), a CSV line:
```
67,2048,2047,2050,2044,2051,2049,1862
```

**Pass criteria:** `lost=0` (or very low if no CPCU connected — every packet will be "lost" since MAX_RT fires). `drop=0` (no DMA overruns). `batt` stable. LED not toggling (LED only toggles in test modes). CSV lines show reasonable ADC values.

**Important note:** Without CPCU, `lost` will equal `ok` (every TX gets MAX_RT). This is expected and not a failure — it just means no receiver is ACKing. The 24 ms MAX_RT timeout per packet means the effective packet rate drops from 667/s to ~40/s. To test at full throughput without CPCU, you'd need to disable auto-ACK or ignore the lost count.

---

### 2.7 BSAU_MODE_RELEASE

**What it tests:** Nothing — this is production firmware. Both LOG and LOG_CSV compile to `(void)0`. No UART output. Minimum code size and maximum performance.

**CPCU needed:** Yes (this is the real system).  
**Analog input needed:** Yes (real EMG electrodes).

**Procedure:**
1. In `bsau_config.h`, uncomment `BSAU_MODE_RELEASE`.
2. Build and flash.
3. Verify: no UART output (silence on terminal).
4. Verify: SWD debugger shows `pkt_count` incrementing, `lost_count` staying zero (with CPCU connected).
5. Verify: current consumption ~116 µA average (measure with µA meter on VDD).

---

## 3. Quick Reference Matrix

| Mode | NRF HW | CPCU | Analog In | UART Use | What you're testing |
|---|---|---|---|---|---|
| `TEST_PKT_LOG` | No | No | No | Terminal (LOG) | WL codec software |
| `TEST_NRF_LOG` | **Yes** | No* | No | Terminal (LOG) | SPI + radio + power cycle |
| `TEST_ADC_CSV` | No | No | Optional | SerialPlot (binary) | ADC + DMA pipeline |
| `TEST_CSV` | No | No | Optional | Terminal/SerialPlot (ASCII) | ADC + DMA pipeline |
| `TEST_DFT_LOG` | No | No | **Yes** | Terminal (LOG) | Analog front-end frequency |
| `DEBUG` | **Yes** | Optional | Optional | Terminal (LOG + CSV) | Full production path |
| `RELEASE` | **Yes** | **Yes** | **Yes** | None (silent) | Production deployment |

*NRF_LOG: CPCU optional — MAX_RT is a valid PASS. Connect CPCU to verify actual RF link (TX_DS).

---

## 4. Common Failure Scenarios

**"NRF_Init FAIL at boot"** → Most likely POR timing. The 200 ms delay should handle it, but if the NRF's VDD ramp is very slow (bad regulator, long RC on power rail), increase `NRF_POR_DELAY_MS`. Also check SPI wiring: SCK (PB3), MISO (PB4), MOSI (PB5), CSN (PA6).

**"Health check FAIL after 10 minutes running fine"** → Thermal issue or loose solder joint. The NRF is near its operating limit at 3.3V / 0 dBm. Check for cold solder on the SPI pins or the NRF module's decoupling cap.

**"ADC values stuck at 0 or 4095"** → ADC input out of range. Check the summing amplifier output. If floating, expect ~2048 (mid-rail). 0 = input shorted to GND. 4095 = input above 3.3V (check your DC offset circuit).

**"drop counter incrementing in CSV mode"** → CPU can't keep up. The UART transmit is blocking and the CSV line is too long. Either reduce `CONSIDERED_CHANNELS_COUNT` or increase baud rate. At 115200 with 6 channels × 3 scans, you're near the bandwidth limit.

**"DFT shows 50 Hz dominant with no signal"** → Mains hum coupling. Check galvanic isolation (HCNR201), ground loop, or shield cable from function generator to PA0.
