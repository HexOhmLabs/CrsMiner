# CRSminer Fleet Scanner — English User Guide

[CRSminer home](../../README.md) · [Ελληνικές οδηγίες](README_GR.md) · [Open the HTML scanner](CRSminer-Fleet-Scan.html)

The **`CRSminer-Fleet-Scan.html`** file is a standalone, browser-based dashboard for monitoring CRSminer devices on a trusted local network. It includes English/Greek interface languages, live hashrate and share counters, charts, configurable card layouts, saved miner addresses, and manual network discovery. Open the HTML file directly in a compatible browser; there is no separate Python service or desktop application to install for this scanner.

## Supported mining devices and ports

| Device | Status endpoint | Address to enter in the miner list |
|---|---|---|
| Classic ESP32 / CYD miner | `http://DEVICE_IP:80/api/status` | `DEVICE_IP` (port 80 is implicit) |
| CRSminer for Android (CPU miner) | `http://PHONE_IP:8080/api/status` | `PHONE_IP:8080` |

The Android miner must be running its local status service, and the phone and the computer viewing the dashboard must be reachable over the same trusted local network. The browser must also permit requests to the miners' HTTP endpoints. The **Android CPU miner** is distinct from the Android *monitor* app.

## Manual addition — fastest way to verify a phone miner

1. Open `CRSminer-Fleet-Scan.html` in your browser and click **Miners**.
2. Enter one device per line. Optionally add a descriptive label after the IP address:

   ```text
   192.168.1.40 ESP32
   192.168.1.41:8080 Android Phone
   ```

3. Click **Save** to retain the addresses and start updating their cards. Replace the sample IPs above with the real IPs of your devices.

If the phone miner is still not displayed, open `http://PHONE_IP:8080/api/status` directly in a browser on the same PC. You should see a JSON response. A dedicated Android scanner being able to find a phone does **not** guarantee that a desktop browser can access the same HTTP server: browser permissions and CORS restrictions can differ.

## Automatic discovery: ESP32 and Android

1. Click **Miners** to open the discovery dialog, or click **Scan network** to open it and start a scan immediately with the current selections.
2. Enter your local IPv4 prefix, e.g. `192.168.1`, and the last-octet range you want to inspect (for example, 1–254). Multiple prefixes may be separated by commas.
3. Select the desired scan mode:
   - **ESP32 + Android (80 + 8080)** — checks both ports.
   - **ESP32 only (80)** — checks only port 80.
   - **Android only (8080)** — checks only port 8080.
4. Click **Scan** when opening the dialog through **Miners**. If you entered through **Scan network**, the scan will already have started; use **Stop** if necessary.
5. Discovered miners appear with their **actual IP and port**. Click **Save** to preserve the list and begin monitoring.

The discovery process probes each selected IP/port combination once during a scan, using up to eight concurrent workers, as in the preceding HTML scanner. Selecting both ports doubles the number of HTTP targets compared with scanning just one port. To troubleshoot a single phone, prefer **Android only (8080)** and a narrow address range. Avoid repeated full-subnet scans, which can put unnecessary pressure on small ESP32 HTTP servers.

## Dashboard controls

- **English / Ελληνικά** switches the interface language.
- **Rows on screen** and **Per row** control card arrangement.
- The refresh selector offers **3 s, 5 s, and 10 s** intervals.
- **Miners** edits the list of known devices; the browser normally remembers it using `localStorage` when that feature is permitted.
- Live cards display status, hashrate, accepted/rejected shares, and related diagnostic information supported by each device.

For conservative, low-impact monitoring on Windows, see the separate [Windows Web Monitor v1.6.1](../Windows_Web_v1.6.1/README_FIRST.txt). This single-file Fleet Scanner is an additional tool, not a replacement for that monitor's more conservative polling and discovery design.

## Browser and network limitations

- A scanner page loaded over **HTTPS** may be blocked from reading miners served over plain **HTTP** (mixed content). Try opening the HTML file locally when allowed, or serve it from a local `http://` origin.
- **CORS** and local-network-access rules differ among browsers. The Android miner supplies `Access-Control-Allow-Origin: *`, but browser policies, network isolation, or permissions may still block requests.
- The scanner cannot obtain the list of devices directly from your router. It tries HTTP requests against the address range you specify.
- Phone IPs can change after Wi-Fi reconnects. A DHCP reservation in your router can help retain the same address.
- The scanner is for **trusted private LANs**. Do not expose miner HTTP endpoints directly to the public internet.

## Source and scope of this update

This guide documents the working **ESP32 + Android** Fleet Scanner that checks ports **80** and **8080**. The bilingual GitHub update includes the same HTML file as the separately tested build and preserves the Greek instructions in [`README_GR.md`](README_GR.md). No firmware, Android application, or mining engine files are modified by this update.
