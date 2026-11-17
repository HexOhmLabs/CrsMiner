# CRSminer ESP32/CYD — πλήρης οδηγός προγραμματισμού μέσω browser

[English version](FLASHING.md) · [Οδηγός λειτουργίας ESP32](ESP32_USER_GUIDE_GR.md) · [Οδηγός λειτουργίας CYD](CYD_USER_GUIDE_GR.md)

Χρησιμοποιήστε το **[ESP Web Tool](https://esptool.spacehuhn.com/)** από desktop Chrome/Edge με υποστήριξη Web Serial, καλώδιο USB **δεδομένων**, και συμβατό **κλασικό ESP32 με flash 4 MB**. Κλείστε πρώτα άλλα προγράμματα που χρησιμοποιούν τη σειριακή θύρα.

**Απαραίτητος έλεγχος hardware:** Το ESP32 binary αφορά κλασικό ESP32 Dev Module. Το CYD binary έχει μεταγλωττιστεί για `esp32:esp32:jczn_2432s028r` — **ESP32-2432S028R, δύο θύρες USB, οθόνη ST7789**. **Μη διασταυρώνετε binaries/partitions** και μη χρησιμοποιείτε αυτά τα έτοιμα αρχεία σε ESP8266, ESP32-S3 ή διαφορετικές παραλλαγές CYD.

## Μέθοδος Α — ενιαίο merged αρχείο (προτεινόμενη)

| Πλακέτα | Αρχείο | Flash address |
|---|---|---|
| Κλασικός ESP32 | [HexOhm_CRSminer_ESP32_Final_Merged.bin](../firmware/ESP32/merged/HexOhm_CRSminer_ESP32_Final_Merged.bin) | **`0x0000`** |
| CYD 2,8″ | [HexOhm_CRSminer_CYD_Final_Merged.bin](../firmware/CYD/merged/HexOhm_CRSminer_CYD_Final_Merged.bin) | **`0x0000`** |

Κάθε αρχείο είναι **4.194.304 bytes (4 MiB)**.

1. Ανοίξτε το [ESP Web Tool](https://esptool.spacehuhn.com/).
2. Συνδέστε USB, πατήστε **Connect** και επιτρέψτε πρόσβαση στη σωστή σειριακή θύρα.
3. Επιλέξτε **ένα** merged `.bin` της πλακέτας σας με offset **`0x0000`**.
4. Πατήστε **Program**, περιμένετε να ολοκληρωθεί και, αν απαιτείται, χρησιμοποιήστε BOOT/RESET για download mode.
5. Επανεκκινήστε. Στη συνέχεια κάντε τις πρώτες ρυθμίσεις Wi-Fi, pool, wallet και worker, όπως αναφέρουν οι οδηγοί χρήσης.

**ΠΡΟΣΟΧΗ:** Το merged ξαναγράφει **ολόκληρη** τη flash 4 MB, περιλαμβανομένης της περιοχής NVS. Θεωρήστε ότι **διαγράφονται οι αποθηκευμένες ρυθμίσεις Wi-Fi και mining**. Κρατήστε αντίγραφο των δικών σας στοιχείων. Αποφύγετε επιπλέον «Erase flash» εκτός αν επιθυμείτε καθαρή εγκατάσταση.

## Μέθοδος Β — τέσσερα χωριστά binaries (προχωρημένοι)

Στο ίδιο πρόγραμμα, για την **ίδια πλακέτα**, χρησιμοποιήστε **τέσσερα αρχεία** με τις ακριβείς διευθύνσεις:

| Συστατικό | Address | ESP32 | CYD |
|---|---|---|---|
| Bootloader | `0x1000` | `CRSminer-14.ino.bootloader.bin` | `CRSminer_CYD-18.ino.bootloader.bin` |
| Partition table | `0x8000` | `CRSminer-14.ino.partitions.bin` | `CRSminer_CYD-18.ino.partitions.bin` |
| Boot app | `0xE000` | `boot_app0.bin` | `boot_app0.bin` |
| Application | `0x10000` | `CRSminer-14.ino.bin` | `CRSminer_CYD-18.ino.bin` |

Θα τα βρείτε στα [`firmware/ESP32/advanced/`](../firmware/ESP32/advanced/) και [`firmware/CYD/advanced/`](../firmware/CYD/advanced/). **Μην προσθέτετε μαζί merged και χωριστά binaries.** Οι διευθύνσεις προέρχονται από τα πραγματικά `flash_args` των builds. Ρυθμίσεις: **DIO, 80 MHz, 4 MB flash**.

### Ενημέρωση μόνο της εφαρμογής (για έμπειρους)

Αν γνωρίζετε με βεβαιότητα ότι η συσκευή έχει **ήδη** το σωστό bootloader/partition table για την ίδια οικογένεια build, μπορεί να χρησιμοποιηθεί **μόνο** το αντίστοιχο application `.bin` στο **`0x10000`**, ώστε συνήθως να μην αντικατασταθεί το NVS. **Όχι** για μετάβαση από ESP32 σε CYD ή ανάμεσα σε διαφορετικά partitions. Το κλασικό ESP32 έχει **δύο OTA slots**: η εγγραφή στο `app0` δεν εγγυάται ότι θα ξεκινήσει αυτό, αν έχει επιλεγεί άλλο slot OTA. Σε αμφιβολία, πλήρης εγκατάσταση και νέα ρύθμιση.

## Διαφορές partitions

| Partition | Κλασικός ESP32 | CYD |
|---|---|---|
| NVS | `0x9000`–`0xDFFF` | `0x9000`–`0xDFFF` |
| `app0` | `0x10000`, μέγεθος `0x140000` | `0x10000`, μέγεθος `0x300000` |
| `app1` | `0x150000`, μέγεθος `0x140000` | **Δεν υπάρχει** |
| SPIFFS | `0x290000`, μέγεθος `0x160000` | `0x310000`, μέγεθος `0xE0000` |
| Coredump | `0x3F0000`, μέγεθος `0x10000` | `0x3F0000`, μέγεθος `0x10000` |

Οι τιμές υπάρχουν στα αντίστοιχα `advanced/partitions.csv`.

## Αντιμετώπιση προβλημάτων

- **Δεν συνδέεται στη COM:** καλώδιο δεδομένων, σωστός USB-UART driver, κλείσιμο Serial Monitor και πιθανή χρήση BOOT/RESET.
- **CYD μαύρο/ανεστραμμένα χρώματα:** επιβεβαιώστε ακριβώς το μοντέλο **δύο USB / ST7789**· άλλα μοντέλα χρειάζονται διαφορετικό build.
- **Επιτυχές flash αλλά χωρίς Wi-Fi:** ξανακάντε την αρχική ρύθμιση, γιατί το merged διαγράφει την προηγούμενη.
- **Boot loop:** τροφοδοσία και συμβατότητα. Αν χρειαστεί, ελέγξτε το serial boot log, χωρίς να κοινοποιήσετε passwords ή ιδιωτικά στοιχεία.

Πηγές: [Τεκμηρίωση Espressif](https://docs.espressif.com/projects/esptool/en/latest/esp32/esptool/flashing-firmware.html) · [ESP Web Tool](https://esptool.spacehuhn.com/).
