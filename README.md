# CRSminer · HexOhm

**Real SHA-256d mining on ESP32, CYD and Raspberry Pi + USB Block Erupter, with companion fleet monitors for Windows, Android and the browser.**

[**Get started**](#choose-your-miner) · [**Build the Raspberry Pi miner**](monitoring/Raspberry_Pi_USB_ASIC/README.md) · [**Flash ESP32/CYD in your browser**](docs/FLASHING.md) · [**Οδηγίες στα Ελληνικά**](docs/ΠΡΟΓΡΑΜΜΑΤΙΣΜΟΣ_GR.md) · [**Monitor your fleet**](#local-monitoring) · [**Implementation gallery**](docs/IMPLEMENTATIONS.md) · [**Source builds**](docs/BUILD_FROM_SOURCE.md)

<p align="center">
  <img src="images/esp32-beautified.png" width="54%" alt="HexOhm CRSminer ESP32 studio presentation" />
  <img src="images/cyd-landscape-beautified-sanitized.png" width="44%" alt="HexOhm CYD mining display studio presentation" />
</p>

<p align="center"><strong>Three working mining platforms · One HexOhm family · Local fleet monitoring</strong></p>

**First public release.** The included ESP32, CYD, Raspberry Pi and monitoring implementations are the working editions confirmed by the project owner. Additional ESP board variants are outside this release. Source code and binaries are provided together wherever relevant.

> The studio device images are presentation artwork based on the actual hardware; use the board-specific flashing and hardware instructions, not artwork silkscreen, to determine compatibility.

## Choose your miner

| ESP32 | CYD (Cheap Yellow Display) |
|---|---|
| <img src="images/esp32-beautified.png" width="440" alt="Classic ESP32 board"/> | <img src="images/cyd-landscape-beautified-sanitized.png" width="440" alt="CYD miner with screen"/> |
| **Final tested build: `CRSminer-14`** | **Final tested build: `CRSminer_CYD-18`** |
| [Download merged `.bin`](firmware/ESP32/merged/HexOhm_CRSminer_ESP32_Final_Merged.bin) | [Download merged `.bin`](firmware/CYD/merged/HexOhm_CRSminer_CYD_Final_Merged.bin) |
| [Arduino sketch](firmware/ESP32/source/CRSminer/CRSminer.ino) · [Separate flash files](firmware/ESP32/advanced/) | [Arduino sketch](firmware/CYD/source/CRSminer_CYD/CRSminer_CYD.ino) · [Separate flash files](firmware/CYD/advanced/) |

### Raspberry Pi + USB Block Erupter — working USB ASIC miner

<table><tr><td width="42%">
<img src="images/raspberry-pi-usb-asic-live.jpg" width="400" alt="Live CRSminer Pi pi01 USB ASIC share statistics and hashrate in the fleet monitor"/>
</td><td>
<strong>CRSminer Pi — working hardware build</strong><br/>
Raspberry Pi running a USB Block Erupter via <strong>cgminer 3.7.2</strong>, with approximately <strong>300 MH/s</strong> reported during actual operation. The live screenshot shows a momentary reading from a separate session; speed can vary with the stick and mining conditions.<br/><br/>
<a href="monitoring/Raspberry_Pi_USB_ASIC/README.md"><strong>Complete SD-card installation and first-boot build guide</strong></a><br/>
<a href="monitoring/Raspberry_Pi_USB_ASIC/boot/">Installation scripts and example configuration</a>
</td></tr></table>

Unlike the ESP32 and CYD builds, this miner is **not flashed with a `.bin`**. The four supplied scripts/configuration files are copied to the Raspberry Pi OS Lite boot partition; the miner installs and compiles cgminer on first boot. The Pi also serves a compatible local `/api/status` endpoint for the fleet monitors.

The correct board matters: **do not exchange ESP32 and CYD binaries**. The CYD binary targets the specific two-USB-port ESP32-2432S028R ST7789 variant described in the sketch; other CYD versions may need recompilation.

### ESP32 browser dashboard

<img src="images/esp-dashboard-sanitized.jpg" width="880" alt="ESP32 miner live web dashboard showing SHA-256 hashrate and shares with network identifiers concealed" />

*Live ESP32 telemetry, including per-core hashrate, shares, difficulty, temperature and pool status. Network identifiers have been concealed.*

## Flash without Arduino IDE

Open **[ESP Web Tool](https://esptool.spacehuhn.com/)** in a Web Serial–capable desktop browser (Chrome or Edge), connect your device by USB, and flash the *matching* merged firmware at **`0x0000`**. Each merged file is a complete **4 MB image**. Flashing it can **erase existing Wi-Fi and miner configuration**, so save your settings first.

For advanced users, the four files are also included in each target's `advanced/` folder:

| Flash address | File purpose |
|---|---|
| `0x1000` | Bootloader |
| `0x8000` | Partition table |
| `0xE000` | `boot_app0.bin` |
| `0x10000` | Application `.bin` |

Read the **[full illustrated-by-tables flashing instructions](docs/FLASHING.md)** before doing a separate-file upload. The two devices have **different partition tables**.

## CYD — on-device display

<img src="images/cyd-portrait-beautified-sanitized.png" width="360" alt="Portrait studio photo of CYD mining dashboard"/>

The CYD variant adds an on-device screen and real-time mining metrics. The example display presentation shows status, worker/miner name, hashrate, share counters, uptime and difficulty. Check compatibility with your exact CYD variant before flashing.

## Local monitoring

<p align="center">
  <img src="images/fleet-monitor-english.jpg" width="66%" alt="CRSminer browser fleet monitor with multiple running ESP miners" />
  <img src="images/android-monitor-sanitized.jpg" width="30%" alt="CRS Mining Android monitor showing a mixed fleet" />
</p>

*Live dashboards from the working monitoring applications. The examples show actual fleet statistics; local-network and wallet values are concealed where they appear.*

| Component | Purpose | Included |
|---|---|---|
| [**CRS Mining Web for Windows v1.6.1**](monitoring/Windows_Web_v1.6.1/) | Staggered safe polling with manual discovery of ESP and compatible Android endpoints | Complete working package |
| [**CRS Mining Android Monitor v1.3**](monitoring/Android_Monitor_v1.3/) | Android Studio source for LAN fleet monitoring and discovery | Working source project |
| [**Browser Fleet Scanner**](monitoring/Browser_Fleet_Scanner/CRSminer-Fleet-Scan.html) | One-file bilingual HTML dashboard with adjustable layout and LAN scanning | Working HTML tool |

For the Windows monitor, **v1.6.1** is the published stable edition. For manual full-network scans, follow the recommended conservative settings; device reliability is the priority.

See [Monitoring](docs/MONITORING.md) for usage and [Implementation gallery](docs/IMPLEMENTATIONS.md) for the complete walkthrough and additional real screenshots.

This first public repository does not include the separate Android CPU-mining application.

## Hardware and functionality

- Real SHA-256d mining via compatible Stratum v1 pools over TCP.
- Hardware SHA on supported classic ESP32 silicon; fallback paths in code as documented in the source.
- WiFiManager setup, four-character miner IDs, configurable pool/wallet/worker and English/Greek web interface.
- Hashrate, accepted/rejected shares, best difficulty, device diagnostics and optional LED behavior.
- CYD live screen for supported display hardware.
- Working Raspberry Pi + USB Block Erupter (~300 MH/s reported), with cgminer 3.7.2 built on first boot.
- HTTP status endpoint `/api/status` on ESP and the Pi status-bridge design (network access only; not a public-facing service).

**Mining is intended for learning and experimentation, not a promise of profit.** These devices have negligible hashrate compared with modern commercial ASIC miners. Pool and wallet configuration is supplied by users. Mining draws energy and generates heat.

## Build / release details

- **ESP32 final source:** `CRSminer-14.ino` from the final archive, published in Arduino-compatible `CRSminer/CRSminer.ino` form.
- **CYD final source:** `CRSminer_CYD-18.ino` from the final archive, published in Arduino-compatible `CRSminer_CYD/CRSminer_CYD.ino` form.
- **Arduino ESP32 core 3.3.12**, 4 MB flash for both binaries. Exact flash maps are from the supplied builds.
- **Raspberry Pi working source:** the supplied four boot files, unchanged from the project owner's `CRSminer_Pi_usbAsic.zip`, with the setup instructions documented [here](monitoring/Raspberry_Pi_USB_ASIC/README.md). No precompiled cgminer is bundled.
- The source files and compiled firmware binaries have been retained byte for byte from the working builds supplied by HexOhm.
- Checksums are available in [`SHA256SUMS.txt`](SHA256SUMS.txt).
- License: [MIT](LICENSE) for the included HexOhm code, with [third-party attributions](CREDITS.md); externally obtained **cgminer is GPL-3.0** and **not redistributed** in this repository.

## More information

[Flashing guide](docs/FLASHING.md) · [Monitoring comparison](docs/MONITORING.md) · [Included releases](docs/VERSIONS_AND_STATUS.md) · [Privacy and safe sharing](docs/SECURITY_AND_PRIVACY.md) · [First publication checklist](docs/RELEASE_CHECKLIST.md)

© 2026 **HexOhm** · Developed with assistance from **AI**.
