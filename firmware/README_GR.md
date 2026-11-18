# CRSminer — Οι πέντε υλοποιήσεις mining

[English](README.md) · [Κεντρική σελίδα](../README_GR.md)

Ο φάκελος `firmware/` φιλοξενεί **και τους πέντε miners**. Ο ξεχωριστός `monitoring/` περιλαμβάνει μόνο εφαρμογές παρακολούθησης.

| Πλατφόρμα | Κώδικας/Οδηγός | Αρχεία |
|---|---|---|
| ESP32 · `CRSminer-14` | [ESP32 οδηγίες](../docs/ESP32_USER_GUIDE_GR.md) | [Arduino `.ino`](ESP32/source/CRSminer/CRSminer.ino) · [merged `.bin`](ESP32/merged/HexOhm_CRSminer_ESP32_Final_Merged.bin) · [advanced flash](ESP32/advanced/) |
| CYD · `CRSminer_CYD-18` | [CYD οδηγίες](../docs/CYD_USER_GUIDE_GR.md) | [Arduino `.ino`](CYD/source/CRSminer_CYD/CRSminer_CYD.ino) · [merged `.bin`](CYD/merged/HexOhm_CRSminer_CYD_Final_Merged.bin) · [advanced flash](CYD/advanced/) |
| Raspberry Pi + USB Block Erupter | [Οδηγός εγκατάστασης](Raspberry_Pi_USB_ASIC/README_GR.md) | [4 boot files](Raspberry_Pi_USB_ASIC/boot/) · cgminer 3.7.2 μεταγλωττίζεται στο Pi |
| Android CPU miner v0.4.0 | [Κώδικας / build](Android_CPU_Miner_v0.4/README_GR.md) | [Java source](Android_CPU_Miner_v0.4/app/src/main/java/com/hexohm/crsminerandroid/) · [C++ native SHA](Android_CPU_Miner_v0.4/app/src/main/cpp/sha256d.cpp) · [debug APK](Android_CPU_Miner_v0.4/apk/CRSminer_for_Android_v0.4.0-dev_DEBUG.apk) |
| Windows PC CPU miner v0.2.3 | [Πηγαίος κώδικας και Windows build](Windows_CPU_Miner_v0.2.3/README_GR.md) | Ενσωματωμένη native x64 DLL και `BUILD_WINDOWS.bat` (όχι έτοιμο EXE) |

Το Raspberry Pi χειρίζεται πραγματικό USB ASIC· δεν χρησιμοποιεί flash `.bin` τύπου ESP32. Ο Android miner εκτελεί πραγματικό SHA-256d και είναι διαφορετική εφαρμογή από τον [Android Monitor](../monitoring/ANDROID_MONITOR_VERSIONS_GR.md).
