# CRSminer Pi — Raspberry Pi + USB Block Erupter (ελληνικός οδηγός)

[English](README.md) · [Όλοι οι miners](../README_GR.md) · [Αρχική σελίδα](../../README_GR.md)

**Επιβεβαιωμένη λειτουργία σε πραγματικό hardware από τον δημιουργό.** Raspberry Pi και USB Block Erupter ASIC, με περίπου **300 MH/s** στην αναφερόμενη λειτουργία. Το πραγματικό hashrate εξαρτάται από το ASIC και τις συνθήκες. Το Raspberry Pi χειρίζεται Wi-Fi, ρυθμίσεις και HTTP status, ενώ **το USB ASIC εκτελεί τον υπολογισμό SHA-256d** μέσω **cgminer 3.7.2**. Δεν συμπεριλαμβάνεται cgminer binary: τα scripts το αποκτούν/μεταγλωττίζουν κατά την εγκατάσταση.

![Πραγματικό Pi USB ASIC σε fleet monitoring](../../images/raspberry-pi-usb-asic-live.jpg)

## Απαιτούμενο hardware

- **Raspberry Pi 3A+** με ενσωματωμένο Wi-Fi, όπως ορίζουν οι πρωτότυπες οδηγίες. Το παλιό Raspberry Pi A+ δεν διαθέτει ενσωματωμένο Wi-Fi· απαιτεί πρόσθετο προσαρμογέα και κατάλληλο powered USB hub.
- **USB Block Erupter** στο USB του Pi ή σε τροφοδοτούμενο hub, αν απαιτείται.
- microSD **8 GB ή μεγαλύτερη**.
- Αξιόπιστη τροφοδοσία Raspberry Pi **5 V / 2,5 A**, με επαρκές ρεύμα για το USB miner.
- Υπολογιστής για προετοιμασία SD και σύνδεση Internet στο Pi για το αρχικό build.

## Προετοιμασία SD και πρώτη εκκίνηση

1. Με το **Raspberry Pi Imager**, γράψτε **Raspberry Pi OS Lite (32-bit)** στη microSD. Οι πρωτότυπες οδηγίες ζητούν να μην παραμετροποιήσετε το Wi-Fi μέσω Imager, επειδή γίνεται από το αρχείο `crsminer.txt`.
2. Ανοίξτε στον υπολογιστή το partition **`bootfs`** και αντιγράψτε **στη ρίζα του** και τα τέσσερα αρχεία του φακέλου [boot/](boot/):

   ```text
   crsminer.txt
   crsminer-run.sh
   crsminer-firstrun.sh
   crsminer-status.py
   ```

3. Ανοίξτε το **`crsminer.txt`** με απλό text editor και ορίστε: όνομα Wi-Fi/κωδικό/χώρα, τετραγράμματο ή τετραψήφιο ID miner, Stratum pool `host:port`, wallet, worker, password pool. Μην δημοσιεύσετε το προσωπικά συμπληρωμένο `crsminer.txt`.
4. Στο partition `bootfs` ανοίξτε το **`cmdline.txt`**. Πρέπει να παραμείνει **μία και μόνο γραμμή**. Στο **τέλος της υπάρχουσας γραμμής**, προσθέστε **ένα κενό** και ακριβώς:

   ```text
   systemd.run=/boot/firmware/crsminer-firstrun.sh systemd.run_success_action=reboot systemd.unit=kernel-command-line.target
   ```

5. Αφαιρέστε με ασφαλή κατάργηση τη microSD, τοποθετήστε την στο Pi, συνδέστε το USB ASIC (ή powered hub) και τροφοδοτήστε το Pi.
6. Στην **πρώτη εκκίνηση**, ο installer αντιγράφει τα scripts στο `/usr/local/bin/`, δημιουργεί/ενεργοποιεί το `crsminer.service`, αφαιρεί το one-time hook από το `cmdline.txt` και επανεκκινεί. Το service εγκαθιστά τα απαραίτητα πακέτα και αποκτά/μεταγλωττίζει **cgminer v3.7.2** με USB Icarus backend εφόσον χρειάζεται. Η διαδικασία θέλει Internet και χρόνο ανάλογα με το Pi.
7. Στις επόμενες εκκινήσεις το service ξεκινά τον miner αυτόματα και επανεκκινεί το mining process αν τερματιστεί.

**Δεν χρησιμοποιούμε ESP Web Tool, merged `.bin` ή διευθύνσεις flash ESP32 στο Raspberry Pi.**

## Πεδία του `crsminer.txt`

| Όνομα ρύθμισης | Περιγραφή |
|---|---|
| `wifi_ssid`, `wifi_password`, `wifi_country` | Wi-Fi και διγράμματος κωδικός χώρας |
| `name` | Ακριβώς 4 λατινικά γράμματα/ψηφία, π.χ. `PI01`, για `crsminer-pi01.local` |
| `pool` | Διεύθυνση Stratum pool με θύρα: `host:port` |
| `wallet` | Συμβατή με το pool διεύθυνση πληρωμών |
| `worker` | Όνομα worker, π.χ. `erupter` |
| `pool_password` | Κωδικός pool, συχνά `x` |
| `lang` | `en` ή `el` για τη σελίδα status |
| `extra_args` | Πρόσθετες προαιρετικές παράμετροι cgminer |

Το αρχείο διαβάζεται όταν ξεκινά ο miner. Για εφαρμογή αλλαγών επανεκκινήστε το Pi.

## Παρακολούθηση και API

Με προσαρμογή του ονόματος `pi01` ή χρήση της σωστής IP:

```text
http://crsminer-pi01.local/
http://<PI-IP-ADDRESS>/
http://<PI-IP-ADDRESS>/api/status
```

Το web status ακούει στη **θύρα HTTP 80** και περιλαμβάνει pool, hashrate, accepted/rejected shares, uptime και θερμοκρασία CPU. Το endpoint `/api/status` δίνει JSON συμβατό με τους fleet monitors. Το cgminer API λειτουργεί **εσωτερικά στη θύρα 4028** και είναι διαφορετικό από την HTTP σελίδα. Χρησιμοποιήστε [εφαρμογές monitoring](../../docs/MONITORING_GR.md) στο ίδιο αξιόπιστο LAN.

## Αντιμετώπιση προβλημάτων

- Log εγκατάστασης/μεταγλώττισης: **`/var/log/crsminer-setup.log`**.
- Εάν δεν υπάρχει Internet στην πρώτη εκκίνηση, τα scripts θα ξαναπροσπαθήσουν αργότερα να ολοκληρώσουν την εγκατάσταση.
- Για σφάλμα ρυθμίσεων ελέγξτε το `crsminer.txt`, διορθώστε και επανεκκινήστε.
- Ελέγξτε τροφοδοσία USB stick/hub, SD και Wi-Fi αν το Pi δεν εμφανίζεται.
- Ελέγξτε ότι το `crsminer.service` ξεκίνησε και ότι ο miner είναι σε εγκεκριμένο pool.

## Άδειες

Τα HexOhm shell/Python scripts είναι διαθέσιμα με την άδεια **MIT**. Το εξωτερικό **cgminer 3.7.2** έχει **GPL-3.0** και αποκτάται/μεταγλωττίζεται ξεχωριστά· **δεν** περιλαμβάνεται εκτελέσιμο cgminer στο repository. Η πραγματική εξόρυξη δεν συνεπάγεται εγγύηση κέρδους.
