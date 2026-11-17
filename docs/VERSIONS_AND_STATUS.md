# Included versions and status

[Ελληνική έκδοση](VERSIONS_AND_STATUS_GR.md)

HexOhm's CRSminer family contains **four mining platforms**: classic ESP32, CYD, Raspberry Pi with a USB Block Erupter, and Android CPU mining. All four miner sources now live in [`firmware/`](../firmware/README.md), while monitors/scanners live in [`monitoring/`](../monitoring/).

| Component | Edition | Status |
|---|---|---|
| Classic ESP32 miner | **CRSminer-14** | Working ESP32 firmware, source and separate/merged flash binaries |
| CYD display miner | **CRSminer_CYD-18** | Working supported CYD firmware, source and separate/merged binaries |
| Raspberry Pi + USB Block Erupter | **CRSminer Pi** | Working USB ASIC implementation, approximately 300 MH/s reported; source and setup scripts |
| CRSminer for Android CPU miner | **v0.4.0** | Working CPU miner; Android Studio source and user-supplied `0.4.0-dev` debug APK |
| Windows Web Monitor | **v1.6.1** | Recommended working safe-discovery edition |
| **Android Monitor/scanner** | **v1.2** | **Recommended, installed and working for the project owner** |
| **Android Monitor/scanner development** | **v1.3-compat-dev** | **Optional source for independent testing**; the owner has not installed/tested it |
| Browser Fleet Scanner | Latest selected bilingual HTML | Working standalone browser scanner |

## Android Monitor distinction

- [Android Monitor v1.2 project](../monitoring/Android_Monitor_v1.2/) — working scanner used by the owner; primarily probes port 80 for ESP miners.
- [Android Monitor v1.3 development project](../monitoring/Android_Monitor_v1.3/) — developer candidate that also probes port 8080 for Android CPU miners and includes local-address discovery improvements.
- [Detailed v1.2/v1.3 comparison and build guidance](../monitoring/ANDROID_MONITOR_VERSIONS.md).

These are distinct from the [Android CPU miner v0.4.0](../firmware/Android_CPU_Miner_v0.4/), which actually performs SHA-256d mining, runs a foreground service, and serves a local status endpoint.

## Publication notes

- The classic ESP32 and CYD flash images have **different board/partition requirements**: never interchange them.
- v1.2 is the recommended Android Monitor. v1.3 is intentionally included **without a claim of physical validation**, for developers who wish to test it.
- The supplied Android CPU miner APK is a **debug** build. It was identified as `com.hexohm.crsminerandroid` / `0.4.0-dev`; no signed release APK is included.
- The Pi installer compiles `cgminer 3.7.2` from its upstream source; cgminer itself is not included in this repository.

See [Flashing](FLASHING.md), [Monitoring](MONITORING.md), [Implementation gallery](IMPLEMENTATIONS.md), and [Android miner source build](../firmware/Android_CPU_Miner_v0.4/README.md).
