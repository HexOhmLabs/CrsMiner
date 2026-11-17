# Εκδόσεις και κατάσταση των εφαρμογών CRSminer

[English version](VERSIONS_AND_STATUS.md) · [Αρχική σελίδα ελληνικά](../README_GR.md)

Το **HexOhm CRSminer** περιλαμβάνει **τέσσερις υλοποιήσεις mining** στο [`firmware/`](../firmware/README_GR.md). Οι εφαρμογές παρακολούθησης είναι ξεχωριστές και βρίσκονται στο [`monitoring/`](../monitoring/).

| Συστατικό | Έκδοση | Κατάσταση |
|---|---|---|
| Κλασικός ESP32 miner | **CRSminer-14** | Λειτουργικό firmware, source, merged και επιμέρους binaries |
| CYD με οθόνη | **CRSminer_CYD-18** | Λειτουργικό firmware για συγκεκριμένο hardware, source και binaries |
| Raspberry Pi + USB Block Erupter | **CRSminer Pi** | Λειτουργικός USB ASIC miner· περίπου **300 MH/s** σύμφωνα με τις δοκιμές· πλήρη scripts |
| Android CPU miner | **v0.4.0** | Λειτουργικός SHA-256d miner· Android Studio source και παρεχόμενο **debug APK** `0.4.0-dev` |
| Windows Web Monitor | **v1.6.1** | Προτεινόμενη δοκιμασμένη σταθερή έκδοση |
| Android Monitor/scanner | **v1.2** | **Προτεινόμενη, εγκατεστημένη και δοκιμασμένη** |
| Android Monitor ανάπτυξης | **v1.3-compat-dev** | **Προαιρετικός κώδικας για δοκιμές τρίτων**, όχι δοκιμασμένος από τον δημιουργό |
| Browser Fleet Scanner | Επιλεγμένο δίγλωσσο HTML | Λειτουργικός scanner σε browser |

### Δύο διαφορετικές Android εφαρμογές

- Ο [Android CPU miner v0.4.0](../firmware/Android_CPU_Miner_v0.4/README_GR.md) **κάνει πραγματικό mining** στη CPU του κινητού και εκθέτει read-only API στο `:8080`.
- Ο [Android Monitor v1.2](../monitoring/Android_Monitor_v1.2/README_GR.md) **παρακολουθεί miners** στο LAN· σαρώνει κυρίως ESP32 στο port 80.
- Ο [Android Monitor v1.3](../monitoring/Android_Monitor_v1.3/README_GR.md) προσθέτει δοκιμαστικό discovery στο port 8080. **Δεν έχει επαληθευτεί στην πράξη από τον δημιουργό.** Διαβάστε τη [σύγκριση](../monitoring/ANDROID_MONITOR_VERSIONS_GR.md).

### Σημειώσεις δημοσίευσης

Τα ESP32/CYD images έχουν διαφορετικό partition layout και **δεν είναι εναλλάξιμα**. Το Android miner APK είναι **debug**, όχι υπογεγραμμένο production release. Το Raspberry Pi κατεβάζει και μεταγλωττίζει **cgminer 3.7.2** στη συσκευή· το cgminer δεν διανέμεται εδώ.

[Flashing](FLASHING_GR.md) · [Monitoring](MONITORING_GR.md) · [Παρουσίαση υλοποιήσεων](IMPLEMENTATIONS_GR.md)
