# Included working editions

This repository is the first public presentation of the **HexOhm CRSminer** family, with **three working mining platforms**: ESP32, CYD and Raspberry Pi + USB Block Erupter. It contains the versions selected, built and **tested in practice by the project owner**, rather than a history of intermediate development files.

| Part | Published build |
|---|---|
| Classic ESP32 miner | **CRSminer-14** — Arduino source, merged 4 MB firmware and individual flash binaries |
| CYD display miner | **CRSminer_CYD-18** — Arduino source, merged 4 MB firmware and individual flash binaries |
| CRS Mining Web for Windows | **v1.6.1** — stable safe monitoring and manual discovery |
| CRS Mining Android Monitor | **v1.3** — local miner discovery and fleet statistics |
| Browser Fleet Scanner | **CRSminer-Fleet-Scan** — bilingual responsive HTML dashboard |
| **Raspberry Pi USB ASIC miner** | **CRSminer Pi** — owner-tested Raspberry Pi + USB Block Erupter (~300 MH/s reported), cgminer 3.7.2 and local status API; [build instructions](../monitoring/Raspberry_Pi_USB_ASIC/README.md) |

The Android CPU-mining app and other hardware/display variants are not part of this initial repository release.

## Publishing policy

- Only the project owner's selected, working editions are distributed.
- The classic ESP32 and CYD firmware images are board-specific and **must not be mixed**.
- Both firmware binaries are retained byte-for-byte from their supplied final builds. The repository also includes `SHA256SUMS.txt`.
- For Windows fleet monitoring, use v1.6.1, which emphasizes reliable, low-impact communication with the miners.
- The Raspberry Pi scripts do not embed the cgminer binary; installation obtains/builds it under its own GPL-3.0 license.

See the [Flashing Guide](FLASHING.md), [Monitoring Guide](MONITORING.md), and [Implementation Gallery](IMPLEMENTATIONS.md).
