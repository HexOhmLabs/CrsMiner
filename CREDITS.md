# Credits and third-party notices

CRSminer is © 2026 HexOhm and released under the [MIT License](LICENSE).

## Code adapted into CRSminer

The **hardware SHA-256 mining loops** for the classic ESP32 (the hand-scheduled Xtensa assembly blocks) and the
**known-answer self-test** used to pick between them are adapted from:

- **NerdMiner Modded** by HenrysCat: <https://github.com/HenrysCat/NerdMiner-Modded>
- **NerdMiner V2** by BitMaker-hub: <https://github.com/BitMaker-hub/NerdMiner_v2>
- **nerdminer-axehub** by dwespl, from which the polled loop descends

These projects are licensed under the MIT License:

```
MIT License

Copyright (c) 2023 Bitmaker
Copyright (c) 2026 HenrysCat (NerdMiner Modded modifications)

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

## Studied, but no code copied

- **SparkMiner** by SneezeGUI (GPL-3.0): <https://github.com/SneezeGUI/SparkMiner>. Read to understand the approach to the ESP32 SHA peripheral. No SparkMiner code is included in CRSminer.

## Libraries used (not included in this repository)

| Library | Author | Licence |
|---|---|---|
| WiFiManager | tzapu | MIT |
| ArduinoJson | Benoit Blanchon | MIT |
| Arduino-ESP32 core | Espressif Systems | LGPL-2.1 |
| LovyanGFX (CYD edition only) | lovyan03 | BSD-style, see its repository |

Each is used under its own licence.

## Raspberry Pi USB ASIC edition

The HexOhm Raspberry Pi boot scripts and compatible web status API are included as working MIT-licensed project source. The installer downloads and builds **cgminer v3.7.2**, maintained in the public `ckolivas/cgminer` repository, under its separate **GPL-3.0** license. This repository does not redistribute cgminer binaries.

## Artwork

The sailor mascot on the CYD display is an original drawing made for this project.

## Development

The hardware behaviour of the supported builds was verified on real boards by the project owner.
