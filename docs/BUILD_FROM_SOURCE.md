# Rebuilding the ESP and CYD sketch sources

Sources under `firmware/ESP32/source/CRSminer/CRSminer.ino` and `firmware/CYD/source/CRSminer_CYD/CRSminer_CYD.ino` are **byte-identical to the author's final submitted .ino files**, renamed to Arduino IDE–compatible sketch folder/file pairs. The `.bin` files were **not regenerated** from these renamed copies.

The author supplied builds made using **Arduino-ESP32 core 3.3.12**:

| Target | FQBN base | Flash | Compiled partition scheme |
|---|---|---|---|
| ESP32 | `esp32:esp32:esp32da` | 4 MB | `default` |
| CYD | `esp32:esp32:jczn_2432s028r` | 4 MB | `huge_app` |

Additional libraries stated in the source headers:

- `WiFiManager` (tzapu)
- `ArduinoJson` v7.x
- `LovyanGFX` for CYD only

The uploaded `flash_args` files explicitly state **DIO, 80 MHz, 4 MB**, and the shipped binaries reflect that build. Some board manager IDE option strings in the archived build metadata list `qio`; to reproduce the supplied *compiled* flashing data, follow the **actual `flash_args`** and included binary headers rather than infer addresses/settings solely from the board selection. The build metadata contains Windows local paths, so it is intentionally not published.

For CYD, the supplied source's current display defaults match the two-USB-port, ST7789-based ESP32-2432S028R variant. Alternate hardware versions require changes and recompilation; the published binary is not universal.
