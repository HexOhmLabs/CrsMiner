# CRSminer mining implementations — HexOhm

[Ελληνική έκδοση](README_GR.md)

This folder contains **all five mining implementations**. The adjacent `monitoring/` folder contains only monitoring/scanning applications.

| Platform | Source code and installation | Additional downloads |
|---|---|---|
| ESP32 · `CRSminer-14` | [Arduino source](ESP32/source/CRSminer/CRSminer.ino) | [Merged flash image](ESP32/merged/HexOhm_CRSminer_ESP32_Final_Merged.bin) · [four separate flash files](ESP32/advanced/) |
| CYD · `CRSminer_CYD-18` | [Arduino source](CYD/source/CRSminer_CYD/CRSminer_CYD.ino) | [Merged flash image](CYD/merged/HexOhm_CRSminer_CYD_Final_Merged.bin) · [four separate flash files](CYD/advanced/) |
| Raspberry Pi · USB Block Erupter | [Installation instructions and original boot scripts](Raspberry_Pi_USB_ASIC/README.md) | [Four boot files](Raspberry_Pi_USB_ASIC/boot/) · cgminer 3.7.2 builds on the Pi |
| Android CPU miner · v0.4.0 | [Android Studio project and build guide](Android_CPU_Miner_v0.4/README.md) | [Java application source](Android_CPU_Miner_v0.4/app/src/main/java/com/hexohm/crsminerandroid/) · [C++ SHA-256d source](Android_CPU_Miner_v0.4/app/src/main/cpp/sha256d.cpp) · [debug APK](Android_CPU_Miner_v0.4/apk/CRSminer_for_Android_v0.4.0-dev_DEBUG.apk) |
| Windows PC CPU miner · v0.2.3 | [Source and Windows build guide](Windows_CPU_Miner_v0.2.3/README.md) | SHA-NI/AVX2/scalar CPU miner · included Windows x64 native DLL, `BUILD_WINDOWS.bat`; no prebuilt EXE |

The Pi USB Block Erupter is an actual mining ASIC controlled by a Raspberry Pi; its software is not an ESP32-style firmware `.bin` file. The Android CPU miner is a separate app from the Android fleet monitor. The Android debug APK is intended for development/testing, not a signed release package.

[Back to repository README](../README.md) · [Monitoring applications](../monitoring/) · [Source build instructions](../docs/BUILD_FROM_SOURCE.md)
