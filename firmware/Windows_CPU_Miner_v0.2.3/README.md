# CRSminer PC v0.2.3 — Windows SHA-256d CPU miner

[Ελληνικές οδηγίες](README_GR.md) · [Source build](#build-a-windows-exe) · [Release notes](CHANGELOG.md) · [Back to HexOhm CRSminer](../../README.md)

**Real SHA-256d mining on Windows x64, designed to use even old laptops and desktop PCs.** A standalone member of the [HexOhm CRSminer](../../README.md) family, separate from the Windows **fleet monitor**. Mining is experimental, not a promise of income.

The owner tested the Windows miner on an **AMD Ryzen 3 3200U** (hardware SHA-NI) and an **AMD E-350** (scalar engine). Both successfully performed mining, submitted accepted shares to a Stratum pool, and were visible in the browser-based Fleet Scanner. Measured hashrates vary with CPU and power management; they are **not** minimums or guarantees.

## Features

- Real **SHA-256d Stratum V1** pool connection, work reception and share submission; pool authorization and accepted/rejected counters.
- Automatically selected **SHA-NI**, **AVX2**, or portable **scalar C** engine, depending on CPU support; all share candidates undergo exact target verification.
- A configurable **0–100% effort setting for each detected logical CPU**. One or more active CPUs required; at least **one logical CPU must be set to 75% or less**. A 0% CPU is not used by the miner.
- **Background** mode uses below-normal Windows priority; **Dedicated** mode uses normal priority with CPU affinity. Neither mode excludes Windows kernel processes or interrupts. Effort is not a hardware power cap.
- Dark/light interface, live hashrate graph, connection and pool metrics, Start/Stop and an application-focus **Ctrl+Shift+Q** emergency stop.
- Optional **diagnostic CSV** recording, direct **Open diagnostics folder**, and confirmed **Clear diagnostic files** (only while stopped).
- Local LAN Fleet API at `http://<PC_IP>:8080/api/status` for compatible CRSminer monitors, including the browser Fleet Scanner. The `.local` hostname in status data is a compatibility label, **not an mDNS registration**.
- **Automatic pool worker naming** in v0.2.3: if the Wallet/account field does not contain `.`, the existing Worker name is appended once for both authorization and share submission.

## Pool configuration

Use **Settings** to enter the Stratum server `hostname:port`, **Wallet / pool account**, **Worker name**, the pool password (if needed), a miner ID and CPU allocation. Save and press **Start**; mining does not start automatically. The configured pool must accept Stratum V1 SHA-256d connections.

| Wallet / pool account | Worker name | Username sent to pool |
|---|---|---|
| `YOUR_WALLET` | `pc01` | `YOUR_WALLET.pc01` |
| `YOUR_WALLET` | `pc02` | `YOUR_WALLET.pc02` |
| `YOUR_WALLET.custom` | `pc02` | `YOUR_WALLET.custom` (unchanged) |

The rule checks for **any period** in the wallet/account field; no extra `.worker` is appended when a period already exists. The stored wallet field is never rewritten. The **pool password is sent as entered**.

## Build a Windows EXE

**The repository contains complete source, the original Windows x64 SHA-256d DLL, tests, and a builder. It does not contain a prebuilt Windows EXE.**

1. Download this entire folder (or the full repository) to a Windows x64 PC, keeping `native/`, `assets/`, `tests/` and all `.py` files together.
2. Install **64-bit Python 3.11 or newer** on the build computer.
3. Double-click **`BUILD_WINDOWS.bat`**. It creates an isolated `.buildvenv`, installs PyInstaller, runs offline tests and a one-worker benchmark, then creates an EXE. Internet access is needed when installing PyInstaller.
4. Run `dist\HexOhm_CRSminer_PC_v0.2.3.exe`. Python is **not required** on machines using this compiled executable.
5. Windows may show a security warning for a locally built unsigned application. Check your build origin before running it; do not disable antivirus or Windows security protections.

For source-mode operation use `RUN_SOURCE_WINDOWS.bat`; for offline performance tests use `BENCH_WINDOWS.bat`. The provided `native/build_native.sh` is optional for developers who wish to rebuild the native DLL; it is not necessary for the normal EXE builder. This package is intended for **Windows x64**, not Linux mining.

## Diagnostics, settings and local monitoring

- Configuration: `%LOCALAPPDATA%\HexOhm\CRSminerPC\config_v023.json`.
- When v0.2.3 is first launched, it can import configuration from v0.2.2 / DIAG2 / older versions **read-only**. It saves its changes in the new v0.2.3 file.
- CSV diagnostics: `%LOCALAPPDATA%\HexOhm\CRSminerPC\diagnostics\`.
- Disable diagnostics in Settings to stop creating *new* CSVs after restarting mining; earlier logs are not deleted. **Clear diagnostic files** asks for confirmation and removes only CRSminer-formatted diagnostic CSV files while mining is stopped.
- Open `http://PC_LAN_IP:8080/api/status` on a device on the same LAN to view live JSON. In the browser Fleet Scanner add the host as **`PC_LAN_IP:8080`**.

## Safety and privacy

- The app's pool password is stored **unencrypted** in local configuration. **Never commit** `config_v023.json`, diagnostics, credentials, wallet-specific screenshots, or runtime logs to GitHub.
- The read-only Fleet API listens on port **8080 on LAN interfaces** with **no authentication or encryption**. It exposes operational data and the configured public wallet/account. Use it on a trusted private network only; do not port-forward it or expose it publicly. Windows Firewall rules should permit only the trusted LAN if needed.
- Mining can cause sustained CPU load, heat and frequency throttling. Monitor temperatures; a low-duty setting is **not** a guaranteed reserved capacity for other applications. Never disable OS kernel functions, thermal limits, or security services for mining.
- Pool shares confirm work is being accepted, **not** expected profits. CPU mining is typically uneconomic compared with modern ASIC miners.

## Testing and licensing

The original v0.2.3 project was owner-tested on two Windows laptops with live pool submissions and LAN fleet monitoring. The local validation in this release environment passed **all 37 offline Python tests**, including the native SHA-256d checks. The Windows EXE has to be built on Windows; it is not provided or signed here.

Application source and bundled native SHA engine: **MIT**, © 2026 HexOhm. See [LICENSE](LICENSE). PyInstaller is a development dependency installed separately by the build script, not bundled as source in this repository. Old UI-preview screenshots were intentionally omitted; do not mistake earlier mockups for the v0.2.3 interface.
