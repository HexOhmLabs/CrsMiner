# CRSminer — Συντελεστές και άδειες τρίτων

[English](CREDITS.md) · [Ελληνική αρχική σελίδα](README_GR.md)

**CRSminer © 2026 HexOhm.** Ο κώδικας HexOhm δημοσιεύεται με την άδεια **[MIT](LICENSE)**. Το πρωτότυπο πλήρες κείμενο της άδειας παραμένει στα αγγλικά ως νομικά δεσμευτικό κείμενο· η παρούσα ελληνική σελίδα είναι ενημερωτική και **δεν** αντικαθιστά το `LICENSE`.

## Προσαρμογές κώδικα SHA-256 στο ESP32

Οι Xtensa assembly hardware-SHA loops του κλασικού ESP32 και ο σχετικός γνωστός-απάντησης αυτοέλεγχος βασίζονται/προσαρμόστηκαν από τα παρακάτω MIT-licensed έργα:

- [NerdMiner Modded](https://github.com/HenrysCat/NerdMiner-Modded) — HenrysCat.
- [NerdMiner V2](https://github.com/BitMaker-hub/NerdMiner_v2) — BitMaker-hub.
- **nerdminer-axehub** — dwespl, προέλευση του polled loop.

Οι αποδόσεις copyright και οι ακριβείς όροι MIT βρίσκονται στο [CREDITS.md](CREDITS.md) και στον πηγαίο κώδικα. Μην αφαιρείτε τις σχετικές ειδοποιήσεις.

## Αναφορά μελέτης, χωρίς αντιγραφή κώδικα

Το **[SparkMiner](https://github.com/SneezeGUI/SparkMiner)** (SneezeGUI, GPL-3.0) μελετήθηκε για την κατανόηση της προσέγγισης SHA peripheral του ESP32· **δεν περιλαμβάνεται** κώδικάς του στο CRSminer.

## Εξωτερικές βιβλιοθήκες

| Βιβλιοθήκη | Συντελεστής | Άδεια |
|---|---|---|
| WiFiManager | tzapu | MIT |
| ArduinoJson | Benoit Blanchon | MIT |
| Arduino-ESP32 core | Espressif Systems | LGPL-2.1 |
| LovyanGFX (μόνο CYD) | lovyan03 | BSD-style, δείτε το upstream repository |

Οι βιβλιοθήκες χρησιμοποιούνται υπό τους δικούς τους όρους, και δεν περιλαμβάνονται απαραιτήτως στον παρόντα φάκελο.

## Raspberry Pi / USB ASIC

Τα scripts εγκατάστασης, διαχείρισης και status API είναι έργο HexOhm υπό MIT. Το εξωτερικό **cgminer 3.7.2** αποκτάται/μεταγλωττίζεται ξεχωριστά και χρησιμοποιεί **GPL-3.0**· δεν διανέμεται compiled binary στο repository.

## Εικαστικά και ανάπτυξη

Ο μικρός χαρακτήρας ναύτη στο CYD είναι πρωτότυπο σχέδιο για το έργο. Η ανάπτυξη έγινε με βοήθεια **AI** και η λειτουργία του hardware έχει επαληθευτεί από τον δημιουργό για τις εγκεκριμένες εκδόσεις. Η v1.3 του Android Monitor παραμένει προαιρετικός, μη δοκιμασμένος από τον δημιουργό πηγαίος κώδικας.
