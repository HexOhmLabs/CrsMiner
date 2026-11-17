# CRSminer Pi — working Raspberry Pi + USB Block Erupter miner

[Ελληνική έκδοση](README_GR.md)

**Status: built and confirmed working on real hardware by the project owner.** This is the headless USB ASIC edition of **CRSminer by HexOhm**. The owner reports approximately **300 MH/s** when using a Raspberry Pi with a USB Block Erupter. Actual instantaneous hashrate varies by USB ASIC and operating conditions.

The Pi manages Wi-Fi, pool/worker configuration and the web API. The **USB Block Erupter performs mining**, using the classic **cgminer 3.7.2** application. The included shell/Python programs are the original working files supplied for the project; cgminer itself is downloaded and compiled by the Pi.

![A real Raspberry Pi Block Erupter reporting mining in a mixed local fleet](../../images/raspberry-pi-usb-asic-live.jpg)

*Real CRSminer `pi01` status seen by the fleet scanner. The display is a snapshot; it is not a fixed benchmark.*

## Hardware required

- **Raspberry Pi 3A+** (the board targeted by the supplied installation instructions) with built-in Wi-Fi. The original Raspberry Pi A+ has no built-in Wi-Fi and would need an adapter and appropriate powered USB hub.
- **USB Block Erupter** ASIC stick, connected to the Pi's USB port or through a suitably powered USB hub when needed.
- microSD card **8 GB or larger**.
- Reliable **5 V / 2.5 A** Raspberry Pi power supply, with sufficient power available to the USB miner.
- A computer with SD-card reader for the first installation, and Internet access for the Pi's initial package installation and cgminer build.

## Installing and building (from the supplied original ZIP)

1. Write **Raspberry Pi OS Lite (32-bit)** to the SD card using Raspberry Pi Imager. The original instructions say not to configure Wi-Fi in Imager, because this package provides its own Wi-Fi setup.
2. On the computer, open the small **`bootfs`** partition of the newly prepared SD card. Copy these **four files** directly from this repository's [`boot/`](boot/) directory into the root of `bootfs`:

   ```text
   crsminer.txt
   crsminer-run.sh
   crsminer-firstrun.sh
   crsminer-status.py
   ```

3. Open **`crsminer.txt`** with a plain text editor and set the values for **Wi-Fi SSID/password and country**, the **four-character alphanumeric miner name**, **pool host:port**, **payout wallet**, **worker name** and **pool password**. The supplied file has example placeholders; replace them with your own values. Do not publish your edited file.
4. Open the SD card's **`cmdline.txt`**. It must remain **one single line**. Append a space and then this exact text to the **end** of the existing line:

   ```text
   systemd.run=/boot/firmware/crsminer-firstrun.sh systemd.run_success_action=reboot systemd.unit=kernel-command-line.target
   ```

5. Safely eject the SD card, insert it into the Pi, connect the USB Block Erupter (directly or through a suitable powered hub) and turn on the Pi.
6. At first boot, the **one-time installer** copies the scripts into `/usr/local/bin/`, installs/enables `crsminer.service`, removes its startup hook from `cmdline.txt` and reboots. The service reads `crsminer.txt`, connects to Wi-Fi and, if necessary, installs dependencies and downloads/builds **cgminer v3.7.2** with the USB Icarus backend. This first-time build requires Internet access and can take several minutes or longer depending on the Pi.
7. Subsequent boots use the installed miner. The mining service is configured to restart automatically if the process exits.

> **No ESP32 flashing tool is needed for the Pi.** The [ESP Web Tool](https://esptool.spacehuhn.com/) and `.bin` files elsewhere in this repository apply only to the ESP32/CYD devices, not to Raspberry Pi.

## Configuration fields

These keys are present in the supplied `boot/crsminer.txt` file:

| Key | Purpose |
|---|---|
| `wifi_ssid`, `wifi_password`, `wifi_country` | Raspberry Pi wireless connection and two-letter country code |
| `name` | Exactly four letters/digits, e.g. `PI01`; becomes hostname `crsminer-pi01.local` |
| `pool` | Mining Stratum host and port, e.g. `pool.example.org:1234` |
| `wallet` | Your pool-compatible payout address |
| `worker` | Worker suffix, e.g. `erupter` |
| `pool_password` | Pool password, typically `x` |
| `lang` | `en` or `el` for the status page |
| `extra_args` | Optional additional cgminer command-line arguments |

The file is read every time the miner starts. After changing settings, restart the Pi to reapply them. Keep your configured Wi-Fi and wallet values private.

## Accessing mining status

On your local network, open either:

```text
http://crsminer-pi01.local/
http://<PI-IP-ADDRESS>/
http://<PI-IP-ADDRESS>/api/status
```

Use your configured miner ID instead of `pi01`. The status page includes pool state, hashrate, accepted/rejected shares, uptime and Pi CPU temperature. The `/api/status` route provides the CRSminer-style JSON used by the fleet monitors; the web service listens on **HTTP port 80**.

The cgminer control API runs on the Pi internally at **port 4028**; it is separate from the HTTP status page.

For fleet visibility, use [Browser Fleet Scanner](../../monitoring/Browser_Fleet_Scanner/CRSminer-Fleet-Scan.html) or the [other monitoring applications](../../docs/MONITORING.md) on a trusted LAN.

## Logs and troubleshooting

- One-time dependency and cgminer build output: **`/var/log/crsminer-setup.log`** on the Pi.
- If the Pi has no Internet during the initial build, the scripts retry the missing installation on a later startup.
- A bad setting generates a **settings error**. Check the SD card's `crsminer.txt` and restart.
- `crsminer.service` manages mining automatically; the installer enables it at the first boot.
- For original Raspberry Pi A+ models without on-board Wi-Fi, the source instructions require appropriate additional Wi-Fi and USB-hub hardware.

## Licensing and scope

The HexOhm setup, management and status scripts are published here under the repository's **MIT license**. The external **cgminer 3.7.2** project uses **GPL-3.0** and is downloaded and built separately at installation time; no cgminer binary is included in this archive.

This is the **working Raspberry Pi mining edition**, not a simulated status-server demonstration. Mining output depends on the ASIC and pool and is not a guarantee of earnings. The scripts have not been modified from the original working ZIP.
