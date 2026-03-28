# InfiniTech: EMG-Driven Prosthetic Hand Controller

**Version:** 6.0 (CPCU) / 2.2 (BSAU) | **Target Architecture:** ARM Cortex-M4 & Cortex-M7

This repository contains the complete firmware stack for the InfiniTech Capstone Project. The system is a highly deterministic, low-latency, and safety-critical embedded pipeline designed to acquire 6-channel electromyography (EMG) signals, transmit them wirelessly, and actuate a 6-DoF prosthetic hand using real-time DSP/ML classification.

---

## System Overview

The system is split across two discrete hardware units communicating via an nRF24L01+ 2.4GHz RF link at 250 kbps:

1. **BSAU (Biopotential Signal Acquisition Unit)**
   * **MCU:** STM32L432KC (Cortex-M4 @ 80 MHz)
   * **Role:** Acquires 6-channel analog EMG data at 2 kHz using hardware oversampling and DMA. Packs the data using a custom 12-bit compression codec into 32-byte payloads and transmits them at ~667 packets/second. Designed for extreme low-power, sleeping between DMA interrupts.
   * **Documentation:** [`BSAU/BSAU_Architecture.md`](./BSAU/BSAU_Architecture.md)

2. **CPCU (Core Processing and Control Unit)**
   * **MCU:** STM32H755ZI-Q (Dual-Core: Cortex-M7 @ 400 MHz + Cortex-M4 @ 200 MHz)
   * **Role (CM4 - Reflex Layer):** Handles deterministic I/O. Manages NRF radio RX, decodes packets, enforces per-joint safety constraints, and drives 6 PWM servo channels.
   * **Role (CM7 - Cognitive Layer):** Handles compute-heavy tasks. Consumes sensor batches, runs DSP/ML threshold classification, and generates motor commands.
   * **Documentation:** [`CPCU/CPCU_ARCHITECTURE.md`](./CPCU/CPCU_ARCHITECTURE.md)

---

## Repository Structure

```text
.
├── BSAU/               # Firmware for the STM32L432 Sensor Node
│   ├── Inc/ & Src/     # App logic, ADC/DMA pipeline, and build configs
│   └── ...             # Architecture and test matrices
├── CPCU/               # Firmware for the STM32H755 Dual-Core Controller
│   ├── CM4/            # Reflex Layer (Radio RX, PWM, Safety clamping)
│   ├── CM7/            # Cognitive Layer (DSP, ML, Inference)
│   └── Common/         # Lock-free SPSC IPC and shared diagnostics
├── nrf24l01/           # Unified nRF24L01+ driver (TX/RX roles) and self-tests
└── wireless/           # Custom 12-bit packet compression/decompression codec
