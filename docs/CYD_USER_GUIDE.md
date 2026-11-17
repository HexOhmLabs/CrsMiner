# CRSminer CYD — setup, touch controls and user guide

[Ελληνική έκδοση](CYD_USER_GUIDE_GR.md)

Applies to **CRSminer_CYD-18**, built for the **two-USB-port ESP32-2432S028R / TPM408-2.8 CYD with ST7789 display**. Check your hardware variant before flashing. This guide follows the published sketch; **no firmware changes** are needed.

## 1. Install and initialize

1. Download the **CYD** merged image from [`firmware/CYD/merged/`](../firmware/CYD/merged/). Do **not** flash the classic ESP32 image.
2. Use the [ESP Web Tool](https://esptool.spacehuhn.com/) with a data-capable USB cable and flash the complete merged 4 MiB image at **`0x0000`**. For four-file flashing see [FLASHING.md](FLASHING.md).
3. Reset the CYD. During startup it shows its own messages, including setup information if configuration is required.
4. When in **SETUP MODE**, follow the Wi-Fi access point name and browser address **shown on the CYD display**. Connect your phone/computer to that access point, then use the WiFiManager portal.
5. Choose Wi-Fi and enter the miner name (**exactly four letters/digits**), compatible Stratum pool (`host:port`), coin-specific wallet/address, worker name and pool password. Optional fields include suggested difficulty, LED mode and language.
6. Save. After connecting, mining proceeds and the CYD displays current values.

The setup AP is normally named `CRSminer-Setup`, or `CRSminer-NAME-Setup` if a valid name exists. In WiFiManager the usual fallback portal address is `http://192.168.4.1`, but **follow the actual address displayed on your CYD**. A merged-image flash erases stored configuration.

## 2. Touchscreen operation

The touch actions are implemented in `ui_update()` in the published CYD sketch. They respond to a **tap (finger released)**.

| What you touch | What happens |
|---|---|
| **Miner character / mascot** on the main mining screen | Starts a **two-minute countdown** to switch off the display backlight. A short visual press indication appears. It does **not** stop mining. |
| **Anywhere else** while the screen is lit | Switches between the **main mining/status screen** and **general information** screen. Tap again to go back. |
| **Anywhere while the screen is dark** | Turns the backlight back on; this first wake-up touch does **not** also switch pages. |

### Main mining screen

Shows the mascot, hashrate, share counters, uptime, a coin/pool-related metric, a moving hashrate graph, and status indicators. The mascot's mood/ring colour responds to mining status.

### General information screen

Displays **Host**, **IP**, **MAC**, **Pool**, **Wi-Fi**, **Chip**, **Cores**, and **Status**. The page itself prompts you to tap anywhere to return.

### What “screensaver” means here

Touching the miner character **does not immediately switch to an animated screensaver**. It schedules the **backlight to turn off after 120 seconds**, conserving display power. The ESP32 continues hashing, remains connected to the pool, and the RGB status LED continues functioning. Touch the screen to wake it again.

## 3. On-board RGB LED feedback

For the supported CYD board, the sketch drives a separate RGB LED on **GPIO4 (red), GPIO16 (green), GPIO17 (blue)**, active LOW. Its event feedback includes:

| Event | RGB LED indication |
|---|---|
| Accepted share count increases | Brief **green** flash (about 150 ms) |
| Rejected share count increases | Brief **red** flash (about 150 ms) |
| Mining/pool not ready | Periodic short **red** flash while offline/not ready |

The mascot ring uses **orange** while working, **green** after a share, and **red** when offline/not ready; **white** is the pressed/touched state. On-board RGB event feedback and the configurable *diagnostic LED pin* are different functions. For this CYD build, the configurable diagnostic LED pin defaults to **`-1`** (disabled), because GPIO2 is reserved for display control. **Do not assign GPIO2 as an LED output on this CYD.**

## 4. Web dashboard and settings

From a phone/computer on the same LAN, open **`http://crsminer-NAME.local`** (replace `NAME` with your four-character miner ID), or use the CYD's displayed IP address: **`http://<miner-IP>`**.

The browser dashboard provides pool and worker configuration, Wi-Fi diagnostics, hashrate, accepted/rejected shares, event history, language and LED options. Changing a setting through **Save & reboot** restarts the miner. **Reset WiFi** in the dashboard clears Wi-Fi credentials and restarts in setup mode, without necessarily wiping all miner preferences.

Use `/api/status` for compatible local monitors. Do not make the miner's web dashboard accessible from the public Internet.

## 5. Factory reset and troubleshooting

- **Full factory reset:** Hold **BOOT during power-up** for **more than three seconds**. The classic ESP32-based CYD firmware uses GPIO0 for this button; stored Wi-Fi and miner preferences are cleared and the device restarts.
- **Incorrect image, inverted colours or no screen:** Verify the **two-USB-port ST7789 CYD** model first. Other versions may require source-level configuration and a separate build, not the published merged image.
- **Touch seems mirrored or misplaced:** Touch-coordinate orientation depends on the supported display/touch controller; the shipped ST7789 build includes a coordinate flip.
- **Nothing happens after touching the mascot:** The *backlight* does not turn off immediately; allow the complete **two-minute** delay.
- **Screen dark but miner still reported by the monitor:** This is expected after the mascot countdown. Tap anywhere to wake the screen.
- **No shares or pool connection:** Check pool URL/port, wallet/worker and status from the web dashboard.

## Source and related guides

[CYD source](../firmware/CYD/source/CRSminer_CYD/CRSminer_CYD.ino) · [Flashing](FLASHING.md) · [ESP32 operating guide](ESP32_USER_GUIDE.md) · [Project home](../README.md)
