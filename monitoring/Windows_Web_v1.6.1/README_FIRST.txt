CRS MINING WEB v1.6.1 — SAFE MONITORING
====================================
2026 by HexOhm
Read-only dashboard for ESP CRSminer (:80) and CRSminer for Android (:8080).

WHY THIS EDITION EXISTS
-----------------------
Earlier web versions scanned entire subnets automatically with many concurrent
HTTP requests. Several ESP miners subsequently displayed diagnostic warnings
and needed restarts. The scan has NOT been proven to be the cause, and v1.6.1 avoids that intrusive behavior by default.

GETTING STARTED ON WINDOWS
--------------------------
1. Keep prior Web v1.4/v1.5 closed. Close its Python window.
2. Extract ALL of this package into a NEW folder.
3. If available, copy known_miners.json from the previous web folder
   beside this version's crs_bridge.py. This imports remembered IPs/ports.
   No wallet/password details are written to known_miners.json.
4. Double-click START_CRS_MINING_WEB.bat (requires Python 3.9+).
5. Open http://127.0.0.1:8765/ if a browser does not open automatically.
6. The dashboard will monitor remembered devices without scanning the LAN.
7. If the saved list is empty or a device has a new IP, use + Add miner with
   its known IP, or click Scan network ONCE for a manual, conservative scan.
   The scan requires confirmation, and a 2-minute cooldown follows completion.

WHAT SAFE MODE DOES
-------------------
- NO full LAN scan at startup and NO scheduled full subnet scans.
- Status requests to saved miners are sequential and staggered (each online
  miner is generally polled no more often than every 12 seconds).
- Offline miners have increasing retry delays, capped at about 120 seconds.
- One missed response does not immediately mark a known miner offline.
- A manual full scan uses up to TWO simultaneous network connections overall,
  and at most one request to each host at a time.
- Regular polling pauses during a manual full scan.
- Scanning does not request miner configuration.
- The worker name and wallet are loaded from /api/config only when you open
  that miner's details, with a short cache to avoid repeated requests.
- The dashboard checks only RFC1918 private IPv4 networks, up to three local
  /24s (or explicitly saved IP addresses), and only HTTP ports 80 and 8080.
- The web bridge binds to localhost:8765; no additional Python packages.
- No commands are sent to stop, start or reconfigure either miner.

ON-SCREEN ENDPOINT LABELS
-------------------------
ESP example:     192.168.1.42 at port 80
Android example: 192.168.1.101 at port 8080

TESTING / TROUBLESHOOTING
-------------------------
- First check the miners' diagnostic LEDs and pool shares with the web
  monitor CLOSED. If they are healthy, start this monitor with saved IPs.
- Allow the saved miners a little time to populate, as polls are staggered.
- Use + Add miner > Test connection to diagnose a known endpoint without a
  full subnet scan. Use :8080 after the Android phone's IP.
- If you need worker or wallet information, open the miner tile to fetch it.
  These fields may initially be blank or say unknown to avoid HTTP bursts.
- An explicit full scan can take noticeably longer than earlier versions;
  reliability is prioritized over scan speed.
- If miner diagnostic LEDs appear or pool shares stop incrementing after
  monitoring starts, immediately close the web server. Record the miner's
  /api/status diag field if reachable. Do not retry full scans repeatedly.

PUBLISHED VERSION
-----------------
v1.6.1 is the tested, working safe-monitoring release selected by HexOhm.
It recognizes ESP and Android miners on the project owner's local fleet.
No monitor can guarantee that every router or firmware variant tolerates all probe traffic.

UNCHANGED
---------
No changes to any ESP firmware, Android mining engine, Android monitor or
its widget. The existing dark/light theme, layout options, port display,
and LAN miner API conventions are preserved from Web v1.4.

V1.6.1 — TARGETED DISCOVERY FIX (2026-10-25)
----------------------------------------
A diagnostic from three real LAN ESP miners confirmed all return HTTP 200
and valid JSON in 0.12–0.18 s through Python urllib. The missing miners were
not due to their IP, RSSI, free heap, or response schema.

In v1.6, discovery opened/closed a bare TCP connection BEFORE performing an
HTTP GET. On a single-client-oriented ESP web server, the first connection
may still occupy the server when the second arrives. Version 1.6.1 uses a
single GET /api/status for each target, with independent 0.30 s TCP connect
and 1.55 s response deadlines, and an explicit Connection: close header.

SAFETY UNCHANGED: no automatic scans, max two in-flight requests, staggered
background polling, no /api/config while scanning. No ESP/Android changes.

For the two missing ESPs, you can also use + Add miner and enter their IPs.
Adding a miner is allowed without running a subnet scan; background polling
will then check it. If a full scan is running, wait for it to finish before
judging an added miner's online status (background polling pauses in scans).

Use a new folder for this edition and copy known_miners.json from the
previous folder if you want to retain your saved devices.
