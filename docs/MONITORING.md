# Monitoring: recommended and development builds

[Ελληνική έκδοση](MONITORING_GR.md)

## Recommended Windows Web Monitor — v1.6.1

In [`monitoring/Windows_Web_v1.6.1/`](../monitoring/Windows_Web_v1.6.1/), run `START_CRS_MINING_WEB.bat` on Windows. It starts a local web dashboard and Python bridge to discover and monitor ESP miners (port 80) and compatible Android CPU miners (port 8080). The selected **v1.6.1** uses conservative requests and staggered polling. It does **not** perform automatic full subnet scans: run manual discovery sparingly, then reuse saved miner IPs.

## Android Monitor/scanner — v1.2 recommended

[`monitoring/Android_Monitor_v1.2/`](../monitoring/Android_Monitor_v1.2/) contains the Android Studio source for the scanner currently used by the project owner. It has a hidden status bar, discovery over Wi-Fi/Ethernet and dashboard/tile options. Its built-in discovery checks ESP endpoints on port **80**; it does not include the optional v1.3 port 8080 enhancement.

**v1.2 is the recommended working edition.** See the [v1.2 README](../monitoring/Android_Monitor_v1.2/README.md) for building.

## Android Monitor/scanner — v1.3 development source

[`monitoring/Android_Monitor_v1.3/`](../monitoring/Android_Monitor_v1.3/) preserves the separate v1.3 Android Studio development project, for those who want to try LAN discovery of an Android CPU miner on **port 8080** as well as ESP miners on **80**. It also closes TCP probes before opening the HTTP status connection and includes the scanner phone's own LAN address.

**v1.3 has not been installed/tested by HexOhm.** It is offered as experimental source rather than the recommended release. Both monitor versions share an Android application ID, so building and installing v1.3 may replace v1.2 on a phone. See [version differences and build notes](../monitoring/ANDROID_MONITOR_VERSIONS.md).

## Browser-only Fleet Scanner

[`monitoring/Browser_Fleet_Scanner/CRSminer-Fleet-Scan.html`](../monitoring/Browser_Fleet_Scanner/CRSminer-Fleet-Scan.html) is the standalone bilingual HTML scanner. Browser rules may limit cross-origin LAN scanning, and aggressive network sweeps can burden smaller ESP HTTP servers. Run full scans deliberately, not continuously.

## Raspberry Pi + USB Block Erupter

The independent **working miner** uses cgminer 3.7.2 and a local `/api/status` bridge, with approximately 300 MH/s observed by the owner. See the [Raspberry Pi installation guide](../firmware/Raspberry_Pi_USB_ASIC/README.md).

## Network API and privacy

ESP miners commonly expose `http://<device-IP>:80/api/status`; the Android CPU miner uses `http://<device-IP>:8080/api/status`. Keep these endpoints on a **trusted local network**, not exposed to the public Internet. Do not upload screenshots showing wallet addresses, internal IPs, or device identifiers.
