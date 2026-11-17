# Flashing the two final CRSminer builds — no Arduino IDE

[Ελληνική έκδοση](FLASHING_GR.md)

Use the **[ESP Web Tool](https://esptool.spacehuhn.com/)** in a desktop Chromium-based browser with Web Serial support (e.g. Chrome or Edge), a data-capable USB cable and a supported **classic ESP32 with 4 MB flash**. The tool runs locally in your browser. Disconnect other serial monitors first.

> ⚠️ **Choose the correct board before flashing.** The **ESP32** build targets the classic ESP32 Dev Module. The **CYD** build was compiled for `esp32:esp32:jczn_2432s028r` (ESP32-2432S028R, the two-USB-port ST7789 variant described in the sketch). The binaries and partitions are **not interchangeable**. Do not flash onto ESP8266, ESP32-S3, a different display/pin mapping, or an unknown-size flash chip.

## Method A — single-file merged firmware (recommended for first install)

| Board | File | Flash address |
|---|---|---|
| Classic ESP32 | [`HexOhm_CRSminer_ESP32_Final_Merged.bin`](../firmware/ESP32/merged/HexOhm_CRSminer_ESP32_Final_Merged.bin) | **`0x0000`** |
| CYD 2.8-inch | [`HexOhm_CRSminer_CYD_Final_Merged.bin`](../firmware/CYD/merged/HexOhm_CRSminer_CYD_Final_Merged.bin) | **`0x0000`** |

Both merged files are **exactly 4,194,304 bytes (4 MiB)** and were produced by Arduino-ESP32. These are the original supplied compiled binaries, **renamed only**.

1. Open [esptool.spacehuhn.com](https://esptool.spacehuhn.com/).
2. Plug the ESP32 into your PC with a data-capable USB cable. Press **Connect** and grant access to the correct serial port.
3. Add the **one merged `.bin` matching your board**; set its offset to `0x0000`.
4. Press **Program** and wait for completion. If needed, enter download mode using the board's BOOT/RESET buttons.
5. Reset or power-cycle the board. Follow the device Wi-Fi configuration portal to set your own Wi-Fi, pool, wallet and worker.

**Important: merged flashing replaces the full 4 MB image.** The merged binary contains `0xFF` in the NVS settings area. Assume saved Wi-Fi and mining settings will be **erased**. Back up your settings before proceeding. Avoid the “Erase flash” command unless intentionally starting completely fresh.

## Method B — individual binary components (advanced)

Choose **four separate files, each with its own address** in the same ESP Web Tool programming operation:

| Component | Address | ESP32 filename | CYD filename |
|---|---|---|---|
| Bootloader | `0x1000` | `CRSminer-14.ino.bootloader.bin` | `CRSminer_CYD-18.ino.bootloader.bin` |
| Partition table | `0x8000` | `CRSminer-14.ino.partitions.bin` | `CRSminer_CYD-18.ino.partitions.bin` |
| Boot app | `0xE000` | `boot_app0.bin` | `boot_app0.bin` |
| Application | `0x10000` | `CRSminer-14.ino.bin` | `CRSminer_CYD-18.ino.bin` |

The files reside in `firmware/ESP32/advanced/` and `firmware/CYD/advanced/`, respectively. Do not select `merged` together with individual components. Offsets were taken from each build's own `flash_args` file. Both builds specify **DIO**, **80 MHz**, and **4 MB flash**.

### Application-only update (advanced, same established layout)

If you are **certain** the device already uses the correct bootloader and partition scheme from this same board/build family, you may write only the *matching* application `.bin` at **`0x10000`**. This generally avoids rewriting the NVS sector. Do not use application-only flashing across incompatible partition tables, or when changing from ESP32 to CYD. The ESP32 build has two OTA app slots; application-only flashing the app0 address is **not a guarantee** that an existing OTA-selected app will be the one booted. For uncertain devices, use a clean full install and reconfigure.

## Actual partition differences

| Partition | Classic ESP32 | CYD |
|---|---|---|
| NVS | `0x9000`–`0xDFFF` | `0x9000`–`0xDFFF` |
| `app0` | starts `0x10000`, size `0x140000` | starts `0x10000`, size `0x300000` |
| `app1` | starts `0x150000`, size `0x140000` | **not present** |
| SPIFFS | starts `0x290000`, size `0x160000` | starts `0x310000`, size `0xE0000` |
| Coredump | starts `0x3F0000`, size `0x10000` | starts `0x3F0000`, size `0x10000` |

Source: the respective `partitions.csv` included under each `advanced` directory.

## Troubleshooting

- Can't connect to COM/serial port: use a USB data cable, close other serial tools, install the USB-UART driver if required and retry BOOT/RESET.
- Black or inverted CYD screen: confirm that you have the **two-USB-port ST7789** board variant; different CYD variants may need a different source configuration and a newly compiled firmware.
- Flash succeeded but no Wi-Fi: do initial Wi-Fi configuration again; merged installs erase previous settings.
- Boot loops: check power supply and board compatibility. If still failing, capture the serial boot log and open a GitHub issue, without sharing Wi-Fi passwords or wallet secrets.

Reference: [Espressif flashing documentation](https://docs.espressif.com/projects/esptool/en/latest/esp32/esptool/flashing-firmware.html); [ESP Web Tool](https://esptool.spacehuhn.com/).
