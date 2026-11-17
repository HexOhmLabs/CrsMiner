# CRSminer for Android — HexOhm

[Ελληνική έκδοση](README_GR.md)

**Version 0.4.0-dev source (HexOhm).** This is real, native SHA-256d CPU hashing with Stratum V1 job handling; **not a simulator**. The project owner successfully built and ran the Android miner on a physical phone and verified pool-accepted shares, with observed performance around 6.6 MH/s on the tested device. The archived source package here includes the previously delivered startup-crash fix and restores the previously requested input-field height correction (48 dp rather than 48 raw pixels). This exact repackaged source has **not been independently rebuilt in the GitHub publication process**; it is not an audited or commercially supported miner.

## Build (Android Studio)
1. Unzip; open the `CRSminer_for_Android` folder in Android Studio.
2. On Windows, run `PREPARE_WINDOWS.bat` once to download/verify the official Gradle wrapper JAR; this project does not bundle that JAR. Install Android SDK Platform 35, Android NDK, and CMake through Tools → SDK Manager if prompted.
3. Sync Gradle; Build → Make Project; Run on an Android 8.0+ device. For sideloading, Build → Generate Signed Bundle / APK.
4. Configure a **unique 4-character alphanumeric Miner ID**, Stratum host:port for pool mode, wallet/pool username, worker and 1+ CPU thread. Save, then press Start.
5. Stop using the in-app Stop button or persistent notification. Android controls may kill the process despite the foreground notification; mining does not start automatically on boot.

## Modes
- Pool mining: choose your Stratum V1 SHA-256d pool endpoint. Example `pool.example.com:3333` is NOT a usable pool; enter a genuine one.
- Solo/lottery mining: uses `stratum.ckpool.org:3333` Stratum protocol with a supported payout address. This relies on a third-party *solo pool*; it is NOT an independently operated Bitcoin Core node. A genuine Bitcoin Core GBT/RPC mining backend would be a separate implementation.
- Extranonce2 allocation per worker; Stratum subscription, authorization, difficulty, notifications, shares, full 256-bit difficulty check and Java cross-check of found nonces.
- Portable native C++ SHA-256d midstate acceleration, adapted from the ESP software SHA architecture (NOT the ESP32-specific hardware engine). The native self-tests include `SHA256("abc")`, the Bitcoin genesis block and optimized-midstate vs full-hash comparisons.
- ESP-style near-miss difficulty tracking (12 leading zero bits) updates **Best difficulty** without waiting for a pool-accepted share. Near misses are rehashed in Java and never submitted unless they really beat the full pool target.
- Clean-job Stratum notifications clear queued stale shares. Includes the ESP's delayed diagnostic checks (`diag`) alongside historical last errors (`err`) and recent authorization replies (`auth`).

## LAN compatibility contract
- ESP-style host label: `crsminer-XXXX.local` (4 alphanumeric character ID).
- Android advertises service name `crsminer-XXXX` as `_http._tcp` using Android `NsdManager`. The actual Android **hostname** is managed by the OS and **may not resolve** to `crsminer-XXXX.local`; use mDNS service discovery or the LAN IP and port.
- `GET http://PHONE_IP:8080/api/status` uses the ESP v8 fields `hr`, `acc`, `rej`, `best`, `pdiff`, `up`, `ok`, `pool`, `temp`, `jobs`, `host`, `mac`, `ip`, `stage`, `mode`, `diag`, `sub`, `rep`, `auth`, `err`; extra `platform:"android"` and `http_port:8080` fields are added.
- `GET http://PHONE_IP:8080/api/config` exposes no password and retains ESP fields `name`, `pool`, `addr`, `worker`, `sugg`; extra platform/mode/thread fields are added.
- **HTTP endpoints are read-only**. There is no unauthenticated remote configuration or start/stop action; edit settings on the Android handset.
- Many Android devices cannot bind port 80; the included separate `CRS_Mining_Android_v1.3_...` optional monitor update supports port 8080. Without that update, the previously compiled CRS Mining Android app scans only TCP 80 and will not see this miner.
- The LAN API is intentionally unsecured. It reveals worker/account settings. Use only on trusted private Wi-Fi; do not port-forward 8080 to the Internet.

## Important limitations
- **Cleartext Stratum TCP v1 only**, not TLS and not Stratum V2; configure a trusted endpoint on networks you trust.
- Supports normal Bitcoin-style SHA-256d Stratum V1, not arbitrary coin-specific fork semantics, auxpow or merged-mining extensions.
- Independent Bitcoin Core `getblocktemplate`/`submitblock` full-node solo is not implemented (solo via solo CKPool only).
- CPU mining is dramatically less efficient than ASIC mining, and may cause significant heat and battery wear. Android system severe-thermal status pauses hashing (device thermal reporting varies).
- Background run uses an explicitly user-started foreground service; Android may still stop it according to device policies.
- Network state tests and live accepted-share verification must be done on real hardware.
- Compatible minimum Android 8.0; target SDK 35 and compile SDK 35. Android 17 local network enforcement is stricter for apps targeting SDK 37+, and future updates will need the relevant permission if target increases.

## Complete Android Studio source files

This folder includes the **full Android CPU miner project**, not only the APK. The native mining code is in [`app/src/main/cpp/sha256d.cpp`](app/src/main/cpp/sha256d.cpp), the foreground Stratum miner in [`MiningService.java`](app/src/main/java/com/hexohm/crsminerandroid/MiningService.java), the interface and latest input-height fix in [`MainActivity.java`](app/src/main/java/com/hexohm/crsminerandroid/MainActivity.java), and the launcher widget in [`MinerWidget.java`](app/src/main/java/com/hexohm/crsminerandroid/MinerWidget.java). Open this directory as a project in Android Studio using `settings.gradle` and follow `PREPARE_WINDOWS.bat` / `BUILD_FIX_README.md` for Gradle setup.

The included debug APK uses package name `com.hexohm.crsminerandroid`, version `0.4.0-dev`. This is not a signed release build; do not assume the published source reproduces the supplied APK byte-for-byte without a controlled rebuild.

## Project separation
- Android miner package: `com.hexohm.crsminerandroid`
- Existing monitoring application: `com.hexohm.crsmining` (not overwritten).
- Neither existing ESP `CRSminer-7.ino` / `CRSminer-8.ino` nor the archived monitor app is modified by this source project.

All source is supplied as a development starting point so firmware and Android integration can be reviewed and tested independently before a production release.

## Review provenance (2026-10-24)
Reviewed against the user's uploaded **latest** `CRSminer.ino` (76 KB, ESP32/S3/C3/C6), not only archived `CRSminer-7.ino` or `CRSminer-8.ino`. ESP GPIO and Xtensa assembly are intentionally not ported to Android. The ESP's 4-character naming, worker separation, Stratum V1 messages, diff1 calculations, delayed diagnostics, near-miss best difficulty, and `/api/status` JSON are the reference conventions. Device discovery uses Android NSD and port 8080 due to Android process constraints. ESP firmware and prior Android monitor remain unchanged. The Android miner UI is still English-only in this experimental package; the ESP's English/Greek preference and an optional `/license` page remain planned compatibility work, not implemented here.

The ESP defaults to `solo.ckpool.org:3333` while this Android project's solo mode uses `stratum.ckpool.org:3333` as currently listed by the solo CKPool service. Android currently supports Stratum V1 **solo through a solo pool**, not independent RPC solo mining.

## Android v0.3 — screen-off mining + home-screen widget (2026-10-24)
- **Top Android status bar hidden only while the main CRSminer activity is visible**; bottom navigation and keyboard remain available. The mining notification is still accessible when the user swipes down or opens another app.
- **CPU background mining** remains a manually started, ongoing foreground service. Adds `WAKE_LOCK` and an acquired `PARTIAL_WAKE_LOCK` while mining, released on Stop/destruction. Helps screen-off CPU operation; never bypasses Android's Doze/network/power-saving policies or severe thermal pause.
- **Home-screen widget (approximately 4 × 2)** shows hashrate, stage, accepted and rejected shares and explicit Start/Stop actions. Tap title/rate to open app. While mining, status refreshes every 10 seconds from the service; when stopped, widget shows zero. The widget has no autonomous background polling and never starts mining without a user tap.
- Widget usage: Android launcher → long press empty home-screen space → Widgets → CRSminer for Android → drag to home screen. Start may fail under manufacturer restrictions; open the app to start if needed.
- Existing pool/solo Stratum logic, native hashing implementation, miner naming, read-only APIs and LAN discovery are unchanged from v0.2. The ESP firmware and separate CRS Mining monitoring app are not modified.
- An owner-tested app and its debug APK are included. Independent source-to-APK reproducibility and device-specific widget/thermal behavior should be checked when rebuilding.

## Android v0.4 — responsive HexOhm widget (2026-10-24)
- Horizontally and vertically resizable, with minimum resize bound 170 × 96 dp.
- Switches automatically between **compact**, **standard**, and **expanded** widget layouts.
- Title, hashrate, status, shares and **button font sizes** adapt to available dimensions; button heights adapt on Android 12+ and change by layout tier on Android 8–11.
- On Android 12+, supplies launcher-reported size-to-layout mappings (`OPTION_APPWIDGET_SIZES`); supports resizing, orientation changes and devices with multiple widget sizes. Falls back to per-widget width/height options on older launchers.
- Actual HexΩhm/HexOhm circuit-logo image from the existing HexOhm branding library, shown as a **very faint 7.5%-opacity** centered watermark behind all the text and controls. This is not the CRS Mining ore emblem used for the app icon.
- Compact layout retains title, hashrate and Start/Stop buttons, hiding secondary text to prevent clipping at minimal sizes.
- Existing mining, pool/solo, foreground service, hashing, local API, device naming, no-auto-start and discovery logic are **identical** to v0.3.
- The project owner has tested the Android miner. For new phones or independently rebuilt APKs, check launcher/widget sizing and device-specific behavior.


## Build-only wrapper fix (2026-10-24)
The first v0.4 package contained an invalid GitHub URL for gradle-wrapper.jar.
Run `PREPARE_WINDOWS.bat` in this corrected package. The new script reuses
a checksum-verified official wrapper from a sibling project when available,
or uses an existing Gradle 8.9 cache, or downloads the official Gradle 8.9
ZIP and uses an isolated Gradle project to generate its wrapper JAR.
The ZIP and generated JAR both get checked against official Gradle SHA-256 values.
No app code, widget layout, project plugin versions or mining engine changed.
Offline test only; not run under Windows/Android Studio in this environment.


## Publication note (HexOhm)

This folder contains the archived Android miner source selected for the initial GitHub update. The main screen input fields use the exact latest `MainActivity.java` fix supplied by the project owner (density-independent heights, font padding and vertical centering). The status-bar startup crash was corrected in the archived build. Build with Android Studio and compare with the installed, personally tested release before distributing a compiled APK. No `.apk` or signing credentials are included in this repository.


## Supplied debug APK

The owner-supplied [CRSminer for Android v0.4.0-dev APK](apk/CRSminer_for_Android_v0.4.0-dev_DEBUG.apk) is provided for development/device testing. It is an Android **debug** build (package `com.hexohm.crsminerandroid`, app version `0.4.0-dev`), not a signed production release. Building the source project may not reproduce this APK byte for byte; no full reproducible-build comparison has been performed.
