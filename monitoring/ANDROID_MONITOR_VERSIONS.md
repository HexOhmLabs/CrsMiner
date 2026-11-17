# CRS Mining Android Monitor: v1.2 and v1.3 development source

The Android **monitor/scanner** is a different app from the **CRSminer Android CPU miner**.

## v1.2 — recommended working monitor

[Complete Android Studio project: v1.2](Android_Monitor_v1.2/)

This is the version currently used and confirmed working by HexOhm. Its Android Gradle metadata states `versionName '1.2'`, `versionCode 3` and package `com.hexohm.crsmining`.

- Discovers compatible ESP miners on **HTTP port 80** on the current Wi-Fi/Ethernet subnet.
- Includes the status-bar-hidden UI, local dashboards, saved miner IPs and adjustable tile layout.
- Does **not** include the proposed Android CPU miner discovery on port 8080.
- Open the project in Android Studio; see its [build README](Android_Monitor_v1.2/README.md).

## v1.3 — optional development source

[Complete Android Studio project: v1.3](Android_Monitor_v1.3/)

This experimental source is provided for anyone who wants to build and evaluate the proposed discovery improvements. **HexOhm has not installed or tested v1.3**, so it is **not** the recommended release.

The development branch adds:

1. Discovery on **port 8080** for a compatible Android CPU miner, while retaining port 80 for ESP miners.
2. Closure of the TCP probe before requesting `/api/status` to avoid holding a single-client server connection open.
3. Inclusion of the phone's own LAN IPv4 address during scanning when the monitor and CPU miner share a phone.

Gradle declares `versionName '1.3-compat-dev'` and `versionCode 4`. **Known documentation/UI mismatch:** the embedded About dialog still says **Version 1.2** because this development package did not change that UI string. It has been left untouched for reproducibility.

Both versions use the **same application ID** (`com.hexohm.crsmining`): installing a v1.3 build may update/replace an existing v1.2 installation. Keep a copy of the working v1.2 build if you experiment, and check Android's package signing rules before changing builds. The `v1.3` source does not have a verified signed APK here.

### Build and LAN safety

Use Android Studio to open the individual project folder containing `settings.gradle`; follow its `README.md` and `PREPARE_WINDOWS.bat` for the Gradle wrapper. Test on an isolated, trusted LAN first. A phone miner must be running with its local API accessible, and Wi-Fi client isolation/firewall settings may prevent discovery.

The development project is supplied as **source for independent testing**, not as a guarantee that its changes work on all Android versions or network setups.
