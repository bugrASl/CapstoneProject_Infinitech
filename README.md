# Project Infinitech Firmware

This repository contains the firmware and architecture for the Infinitech Capstone Project. It is divided into distinct processing units and communication modules.

## Repository Structure

* **`BSAU/`**: Contains the source, headers, and architectural documentation for the BSAU module. See [`BSAU_Architecture.md`](./BSAU/BSAU_Architecture.md) for detailed system design.
* **`CPCU/`**: Contains the Core Processing and Control Unit firmware, split across Cortex-M4 and Cortex-M7 cores (`CM4/` and `CM7/`), plus shared IPC logic. See [`CPCU_ARCHITECTURE.md`](./CPCU/CPCU_ARCHITECTURE.md).
* **`nrf24l01/`**: Drivers and test profiles for the nRF24L01+ wireless transceiver.
* **`wireless/`**: Higher-level wireless packet handling and protocol definitions.

## Development Setup
*(Add instructions here on how to build the project, e.g., Makefile, CMake, or STM32CubeIDE setup).*

## License
This project is licensed under the [MIT License](LICENSE).
