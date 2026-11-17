# CRS Mining Monitor v1.3 · ESP and Android miner discovery

Derived from user's archived v1.2 monitor source. Adds TCP port **8080** detection in addition to unchanged ESP32 port **80**, preserving all existing monitoring UI and endpoint formats. No existing firmware is altered; this project retains the package `com.hexohm.crsmining` and, once built and installed, updates the existing monitor.

**Working discovery-fixed edition confirmed by the project owner.** The miner is discoverable only when the phone and monitor are on the same routable private Wi-Fi network, the Android mining service is running and router client isolation/firewall permits inbound TCP 8080. Some Android devices restrict inbound phone-to-phone connectivity.

Android 17 local network runtime permission already supported by original source.

# CRS Mining

**Native Android monitor for CRSminer ESP32 and ESP32-S3 devices**  
**Version 1.3 · 2026 · by HexOhm (HexΩhm)**

The app automatically scans the phone's active Wi-Fi or Ethernet LAN for compatible CRSminer devices. It connects **directly** to each device's existing HTTP API. No external servers, accounts or password storage are used.

## Features

- HexΩhm splash screen with a **four-second** timer and the separate logo elements (Ω with PCB traces + wordmark).
- Scans the attached private IPv4 subnet (up to 1,024 candidate IP addresses on large subnets) for HTTP port 80 and checks for the actual CRSminer API signature. **Only** devices with the `crsminer-XXXX.local` host pattern and the expected API keys are shown.
- Each compact tile shows the miner's `.local` URL followed by the worker name, large hash rate with auto-scaled H/s units, a recent mini trend graph, green accepted share count, red rejected share count, pool URL, and coin wallet address.
- Green = connected and hashing, amber = attention/pool offline/not hashing, red = unreachable or critical miner diagnostic.
- Top summary: current fleet hash rate, online device count and accepted/rejected shares.
- Settings in ⋮ overflow menu: **Day/Night**, **2/3/4 tiles per row**, **About → Version 1.1 · 2026 · by HexOhm**.
- Auto-refresh every **five seconds** while the screen is open; tap ↻ to rescan the subnet.
- Tap a tile for extended details (uptime, best difficulty, pool difficulty, RSSI, ESP32 reported temperature, SHA engine mode, jobs, errors, and diagnostics); open the miner's existing dashboard from that dialog.
- Long-press a tile or use the detail dialog to copy the **full** configured wallet address. Long addresses on tiles are shortened **only for display**.
- Remembers previously detected device **IPs only** to speed up reconnection. The app does not persist any wallet strings, usernames, or pool details.

## Compatibility with CRSminer firmware

Designed directly from the uploaded **CRSminer-8.ino** source. No firmware changes are required.

Data source | Fields read | Why
--- | --- | ---
`GET http://<ip>/api/status` | `host`, `hr`, `acc`, `rej`, `ok`, `pool`, `best`, `pdiff`, `up`, `rssi`, `temp`, `jobs`, `mode`, `stage`, `diag`, `err`, `mac` | Telemetry/health; device identity
`GET http://<ip>/api/config` | `worker`, `pool`, `addr` | Worker, configured pool, coin wallet address

The app never writes to `/api/config` or calls any reboot/reset endpoint. **It cannot control, reconfigure, or overclock a miner.** The firmware does not publish power consumption, so no W, J/TH or power-efficiency readings are invented. The ESP32's `temperatureRead()` is labeled as **ESP32-reported temperature**, not chip hotspot temperature.

## Build on Windows using Android Studio (first-time setup)

**If you received an earlier ZIP, use this repaired package instead.** That package omitted the Gradle wrapper launcher and JAR.

1. Extract this ZIP (do not open the ZIP itself) to e.g. `C:\AndroidProjects\CRS_Mining_Android`.
2. In File Explorer, **double-click `PREPARE_WINDOWS.bat`** in the `CRS_Mining_Android` folder. It fetches the official Gradle 9.5.0 wrapper JAR and checks the SHA-256 hash against Gradle's official checksum; no third-party wrapper is used. This needs Internet access and Windows PowerShell.
3. When it says **Preparation successful**, open Android Studio and choose **Open** (not **New Project**). Select the inner `CRS_Mining_Android` directory that contains `settings.gradle` and `gradlew.bat`.
4. Android Studio will sync the project using **Android Gradle Plugin 9.3.0**, **Gradle 9.5.0**, and **Android SDK Platform 37**. Use Gradle JDK **17 or newer supported JDK**; JDK 17 is the documented baseline. Install SDK Platform 37 if asked. The first sync requires Internet access for Gradle/plugin dependencies.
5. When the sync succeeds, choose **Build → Build Bundle(s) / APK(s) → Build APK(s)** (menu wording may vary), then find `app\build\outputs\apk\debug\app-debug.apk`.
6. Transfer the debug APK to your phone and install it through your file manager, granting **Install unknown apps** to that file manager if Android prompts. Connect the phone to the same LAN as your miners.

**This repository provides the working Android Studio source, not a precompiled APK.** Build it as described above. If setup fails on your PC, consult the first error from Gradle or Android Studio before changing SDK versions or dependencies.

## Networking and security

- The app only scans local Wi-Fi/Ethernet IPv4 addresses. It will not scan the cellular network, the Internet or VPN ranges. On a subnet larger than `/22`, it scans at most a 1,024-address window around the phone. Routed VLANs / guest networks with client isolation may not permit discovery.
- HTTP port **80** and cleartext traffic are required because the existing CRSminer firmware serves HTTP. The manifest deliberately permits cleartext requests. Do not expose the miners to the public Internet.
- On **Android 17 / target API 37**, the user must grant `ACCESS_LOCAL_NETWORK` at runtime before scanning; earlier Android versions use their standard Internet permission. Permission behavior follows [Android's documentation](https://developer.android.com/privacy-and-security/local-network-permission).
- The `addr` field is a public cryptocurrency receive address from the miner's `/api/config`. This field is readable by other devices on the same LAN as long as the miner's web API is accessible. The app does not cache this value to permanent storage.
- CRSminer has no authentication on those local GET endpoints in firmware version 8. Use a trusted Wi-Fi network.
- If the miner's Wi-Fi IP changes, tapping ↻ discovers it again. The app does not guarantee detection outside the phone's connected subnet.

## Independent design

CRS Mining is an original read-only dashboard, not a copy of HashWatcher's source code or visual assets. It adopts common fleet-monitoring ideas such as color-coded device health, prominent hash rates, and compact fleet tiles, limited to the metrics CRSminer firmware makes available. Not affiliated with HashWatcher or its developer.

## License

Copyright (c) 2026 HexOhm. MIT License, see [LICENSE](LICENSE).

### Changes in 1.1

- Healthy miners show **MINING** even if the firmware still contains an old `err` message (e.g. `job not found`).
- Current connectivity problems, missing hashing and active diagnostic warnings still determine the tile status and color. Historical errors remain accessible under **Last error** when you tap a miner tile.
- Square launcher icon uses the gold-and-black CRS MINING emblem on a **transparent** background; the four-second HexΩhm splash remains unchanged.

To update an existing installation, build the debug APK from this project as usual. The applicationId remains unchanged and `versionCode` has increased to 2. If an existing debug install was signed with a different debug key, Android may require uninstalling the old debug app first.


## v1.2 – status bar display

The Android status bar (clock, signal, battery, and notifications) is hidden while CRS Mining is open, including on the HexOhm splash screen. The Android navigation bar remains available. This prevents the `CRS Mining` dashboard title from overlapping system indicators, including when Android displays apps edge-to-edge. The change is implemented only through `android:windowFullscreen` in both app themes. No miner-monitoring or network behavior was changed.
