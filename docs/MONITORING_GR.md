# Παρακολούθηση miners — σταθερές και δοκιμαστικές εκδόσεις

[English version](MONITORING.md) · [Εκδόσεις στα ελληνικά](VERSIONS_AND_STATUS_GR.md)

## Windows Web Monitor — v1.6.1 (προτεινόμενη)

Στο [`monitoring/Windows_Web_v1.6.1/`](../monitoring/Windows_Web_v1.6.1/) εκτελέστε το **`START_CRS_MINING_WEB.bat`** στα Windows. Ανοίγει τοπικό dashboard μέσω Python bridge, για ESP miners στο port **80** και συμβατούς Android CPU miners στο **8080**. Η v1.6.1 χρησιμοποιεί **ήπια polling, διαφορετικούς χρόνους επανάληψης και χειροκίνητο scan**, χωρίς αυτόματη σάρωση ολόκληρου subnet. Διαβάστε τον [αναλυτικό ελληνικό οδηγό Windows](../monitoring/Windows_Web_v1.6.1/README_GR.md).

## Android Monitor v1.2 — προτεινόμενη

Ο πηγαίος κώδικας στο [`Android_Monitor_v1.2/`](../monitoring/Android_Monitor_v1.2/) αφορά τον **εγκατεστημένο και λειτουργικό scanner** του δημιουργού. Ανακαλύπτει ESP endpoints στο port **80** σε Wi-Fi/Ethernet, προσφέρει tiles, συνολικά στατιστικά, επιλογές διάταξης και κρυμμένη status bar. Η v1.2 **δεν** περιλαμβάνει discovery Android CPU miners στο port 8080. [Εγκατάσταση/μεταγλώττιση στα ελληνικά](../monitoring/Android_Monitor_v1.2/README_GR.md).

## Android Monitor v1.3 — προαιρετικό development source

Ο πηγαίος κώδικας στο [`Android_Monitor_v1.3/`](../monitoring/Android_Monitor_v1.3/) δοκιμάζει port **8080** μαζί με **80**, κλείνει το TCP probe πριν το HTTP request και συμπεριλαμβάνει την τοπική IP του ίδιου του κινητού. **Δεν έχει εγκατασταθεί/δοκιμαστεί από τον HexOhm**. Το application ID είναι το ίδιο με τη v1.2, επομένως η εγκατάσταση μπορεί να την αντικαταστήσει. [Οδηγός v1.3 στα ελληνικά](../monitoring/Android_Monitor_v1.3/README_GR.md) · [Διαφορές εκδόσεων](../monitoring/ANDROID_MONITOR_VERSIONS_GR.md).

## Browser Fleet Scanner

Το [`CRSminer-Fleet-Scan.html`](../monitoring/Browser_Fleet_Scanner/CRSminer-Fleet-Scan.html) είναι αυτόνομος δίγλωσσος HTML scanner. Περιορισμοί του browser (CORS/HTTP) ίσως εμποδίσουν LAN discovery· συχνές μαζικές σαρώσεις μπορεί να επιβαρύνουν τους μικρούς ESP servers. Κάντε πλήρη σάρωση **μόνο όταν χρειάζεται**.

## Raspberry Pi + USB Block Erupter

Είναι **ανεξάρτητος miner**, όχι monitor. Χρησιμοποιεί `cgminer 3.7.2`, έχει τοπική γέφυρα `/api/status` και απέδωσε περίπου 300 MH/s κατά τις δοκιμές. [Οδηγός Raspberry Pi στα ελληνικά](../firmware/Raspberry_Pi_USB_ASIC/README_GR.md).

## Τοπικό API και απόρρητο

- ESP: `http://<IP>:80/api/status`
- Android CPU miner: `http://<IP>:8080/api/status`

Χρησιμοποιήστε **μόνο έμπιστο LAN**, χωρίς port forwarding προς το Διαδίκτυο. Προτού μοιραστείτε screenshots, καλύψτε IP/MAC/wallet και στοιχεία λογαριασμών.
