# CRSminer ESP32 — setup and user guide

[Ελληνική έκδοση](ESP32_USER_GUIDE_GR.md)

Applies to **CRSminer-14** for a compatible classic ESP32 development board. This is an **operation guide**, not a substitute for the [firmware flashing guide](FLASHING.md). The descriptions below are based on the published `CRSminer-14` sketch; no firmware modifications are required.

## 1. Install the firmware

1. Download the **ESP32** merged firmware from [`firmware/ESP32/merged/`](../firmware/ESP32/merged/), **not** the CYD firmware.
2. Use the [ESP Web Tool](https://esptool.spacehuhn.com/) in a desktop browser supporting Web Serial, with a USB data cable.
3. For the complete merged 4 MiB binary, use flash address **`0x0000`**. The alternative four-file addresses are documented in [FLASHING.md](FLASHING.md).
4. Reset the ESP32 after programming.

**Important:** Flashing the complete merged image erases stored configuration. Never interchange ESP32 and CYD images.

## 2. Initial Wi-Fi, pool, and worker setup

When a valid miner name and wallet configuration are missing, CRSminer opens a WiFiManager access point named **`CRSminer-Setup`** (or **`CRSminer-NAME-Setup`** if a four-character name has already been configured).

1. Connect a phone or computer to the access point created by the ESP32.
2. Open the captive portal when prompted. If it does not appear, try the usual WiFiManager address **`http://192.168.4.1`** while connected to that access point.
3. Select your home Wi-Fi network and provide its password.
4. Complete the miner settings: **miner name** (exactly 4 letters/digits, e.g. `rig1`), **pool** (`host:port`), **wallet/address** appropriate to the coin and pool, **worker name**, and **pool password** (often `x`, as specified by your pool).
5. Optional settings include suggested pool difficulty, LED mode, LED GPIO and English/Greek UI language.
6. Save the configuration. The miner connects to your Wi-Fi and pool and starts working after its initialization checks.

A working Wi-Fi connection alone does not guarantee pool authorization. Verify connectivity and accepted shares in the dashboard. The program expects a valid wallet/address and a four-character miner name before it leaves setup mode.

## 3. Open the miner dashboard

From a device on the **same local network**, open:

- **`http://crsminer-rig1.local`** — replace `rig1` with the four-character miner name you selected; or
- **`http://<miner-IP>`** — the IP address assigned by your router, useful if `.local` discovery does not work.

The dashboard reports total and per-core hashrate, accepted/rejected shares, uptime, best and pool difficulty, pool and Wi-Fi state, SHA engine/status, diagnostics, an event log, and a five-minute hashrate graph. The **Settings** area lets you edit miner name, pool, wallet, worker, suggested difficulty, LED options, and language; **Save & reboot** applies changes. The **Reset WiFi** button clears Wi-Fi credentials and restarts into Wi-Fi setup mode; it is different from a full factory reset.

The read-only status endpoint is **`http://<miner-IP>/api/status`** for compatible local fleet monitors. Do **not** expose the miner's dashboard or configuration service to the public Internet.

## 4. Blue LED: normal, off, and diagnostic modes

Select the LED setting in the setup portal or web dashboard. In the classic ESP32 configuration, the **default LED GPIO is 2**, but not every board has its LED there. Configure the actual output-capable pin for your board; **`-1`** means there is no configurable LED. Some board LEDs have different electrical polarity; check your hardware.

| Setting | Actual firmware behaviour |
|---|---|
| **On (default)** | The firmware leaves the chosen LED GPIO alone rather than imposing a diagnostic pattern. This setting does **not** guarantee a continuously illuminated LED. |
| **Off** | The firmware drives the configured LED to the off state and suppresses routine serial status messages. |
| **Diagnostic** | The LED alternates on/off during roughly the **first 20 seconds after boot** when no diagnostic problem has been detected. It then goes dark if all is well. If a persistent condition needs attention, it turns **steadily on** while the condition persists. |

**Steady LED in Diagnostic mode** means check the **Diagnostics** and **Event log** sections of the web interface. The checks include:

- SHA self-test failure;
- Wi-Fi not connected;
- unreachable mining pool or rejected login (including an incorrect wallet/worker setting);
- connected pool but absent hashing;
- excessive rejected shares, or no accepted share for unusually long relative to reported hashrate and difficulty.

The firmware applies condition-specific delays before reporting a problem to avoid reacting to brief startup/network interruptions. A dark diagnostic LED after startup does **not** mean mining has stopped: check the hashrate and pool/share statistics.

## 5. Reset and recovery

- **Change pool / worker / LED options:** Open the web dashboard's **Settings** and select **Save & reboot**.
- **Forget Wi-Fi:** Use **Reset WiFi** in the web dashboard. This clears Wi-FiManager credentials and restarts; mining settings may remain stored.
- **Factory reset:** Hold the board's **BOOT** button while powering on the miner for **more than 3 seconds**. On the classic ESP32 this is the GPIO0 boot button. The sketch clears Wi-FiManager settings and stored miner preferences, then reboots. You will need to configure the miner again.

Avoid performing the BOOT procedure during firmware flashing. Some boards expose BOOT/RESET buttons differently.

## 6. Troubleshooting checklist

| Symptom | What to check |
|---|---|
| Can't see `CRSminer-Setup` | Power/USB cable, serial boot output at 115200 baud, and whether settings were already saved and the miner connected to your Wi-Fi. |
| Can't open `.local` address | Use the IP shown by your router or serial output; the client must be on the same LAN. |
| Connected to Wi-Fi but not mining | Check pool host/port, wallet and worker, pool status and diagnostics in the dashboard. |
| Diagnostic LED stays on | Read the specific diagnostic reason; the LED identifies an **attention** state, not which condition failed. |
| LED seems absent or inverted | Check the correct GPIO, actual board LED wiring/polarity, and whether LED pin is set to `-1`. |
| Unexpected reboot | Check supply stability, USB cable and the dashboard's restart/event information. |

## Source and related guides

[ESP32 source](../firmware/ESP32/source/CRSminer/CRSminer.ino) · [Flashing](FLASHING.md) · [CYD operating guide](CYD_USER_GUIDE.md) · [Project home](../README.md)
