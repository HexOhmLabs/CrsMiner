# Monitoring: what to run

## Recommended Windows Web Monitor — v1.6.1

Under `monitoring/Windows_Web_v1.6.1/`, run `START_CRS_MINING_WEB.bat` on Windows. It hosts a local dashboard through its Python bridge and shows ESP miners (HTTP port 80) and a compatible Android mining service (port 8080) in the same fleet. It uses one HTTP request per discovery attempt and keeps traffic conservative, based on project testing. It **does not** perform automatic full subnet scans. Save discovered miners and use a manual scan only when needed.

Recommended published choice: **v1.6.1**. Use its manual scan sparingly; normal monitoring reuses saved miner addresses.

## Android Monitor — v1.3

`monitoring/Android_Monitor_v1.3/` contains the Android Studio source package that was previously delivered as the discovery-fixed v1.3 release. Open it in Android Studio to build; consult its local README and Gradle setup notes. This is a **monitor**, not the Android CPU miner.

## Browser-only Fleet Scan (supplementary)

`monitoring/Browser_Fleet_Scanner/CRSminer-Fleet-Scan.html` is a standalone HTML scanner supplied by the author. It can monitor and scan visible classic ESP HTTP servers on port 80 from a compatible browser, subject to browser CORS/HTTP restrictions. Its full scan uses eight concurrent workers, and short intervals may put additional load on ESP HTTP servers; use this **only deliberately**, do not repeatedly full-scan the whole subnet while mining is critical. This tool is supplementary, **not** a replacement for the recommended safe v1.6.1 Windows bridge.

## Raspberry Pi USB ASIC miner — independently tested

The **working** Raspberry Pi + USB Block Erupter is a mining device (approximately 300 MH/s reported by the project owner), **not merely a monitoring bridge**. It uses cgminer 3.7.2 to operate the USB ASIC and publishes the CRSminer-compatible `/api/status` over the LAN so the existing monitors can display it.

[Complete Raspberry Pi OS Lite setup and first-boot cgminer build guide](../monitoring/Raspberry_Pi_USB_ASIC/README.md). The cgminer binary is **not** redistributed; the scripts fetch and build it under its own **GPL-3.0** license.

## Common LAN status API

ESP miners: `http://<device-IP>:80/api/status`.

Some companion miners may expose `http://<device-IP>:8080/api/status`.

Limit remote use to a trusted local network. Avoid repeatedly querying every address. No services here should be placed directly on the public Internet without network access controls.

For illustrations of each working component, see the [implementation gallery](IMPLEMENTATIONS.md).
