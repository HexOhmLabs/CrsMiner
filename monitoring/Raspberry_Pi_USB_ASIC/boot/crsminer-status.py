#!/usr/bin/env python3
# CRSminer Pi status page. Reads the miner's local API (cgminer, port 4028) and serves:
#   /            small dashboard        /api/status   JSON in the same shape as the ESP32 miners (Fleet pages can read it)
import json, os, socket, time, http.server

NAME = os.environ.get("CRS_NAME", "crsminer"); POOL = os.environ.get("CRS_POOL", ""); LANG = os.environ.get("CRS_LANG", "en")
T0 = time.time()

def api(cmd):
    try:
        s = socket.create_connection(("127.0.0.1", 4028), 2); s.sendall(cmd.encode()); s.shutdown(socket.SHUT_WR)
        b = b""
        while True:
            d = s.recv(4096)
            if not d: break
            b += d
        s.close()
        out = []
        for rec in b.decode("latin1").strip("\x00").split("|")[1:]:
            row = {}
            for kv in rec.split(","):
                if "=" in kv: k, v = kv.split("=", 1); row[k] = v
            if row: out.append(row)
        return out
    except Exception:
        return []

def f(d, *keys):
    for k in keys:
        try: return float(d[k])
        except Exception: pass
    return 0.0

def cpu_temp():
    try: return round(int(open("/sys/class/thermal/thermal_zone0/temp").read()) / 1000, 1)
    except Exception: return 0.0

def rssi():
    try:
        for l in open("/proc/net/wireless").read().splitlines()[2:]: return int(float(l.split()[3]))
    except Exception: pass
    return 0

def my_ip():
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM); s.connect(("1.1.1.1", 80)); ip = s.getsockname()[0]; s.close(); return ip
    except Exception: return ""

def status():
    summ = (api("summary") or [{}])[0]; devs = api("devs"); pools = api("pools")
    err = ""
    try: err = open("/run/crsminer-error").read().strip()
    except Exception: pass
    alive = any(p.get("Status") == "Alive" for p in pools)
    hr = f(summ, "MHS 5s", "MHS av") * 1e6
    stage = "mining" if (hr > 0 and alive) else ("connecting to pool" if devs else "no Block Erupter found")
    if not summ: stage = "starting"
    if err: stage = "settings error"
    cores = [round(f(d, "MHS 5s", "MHS av") * 1e6) for d in devs]
    return {"hr": round(hr), "cores": cores, "acc": int(f(summ, "Accepted")), "rej": int(f(summ, "Rejected")),
            "best": f(summ, "Best Share"), "pdiff": 0, "up": int(time.time() - T0),
            "ok": alive, "pool": POOL.split(":")[0], "rssi": rssi(), "heap": 0, "temp": cpu_temp(), "jobs": int(f(summ, "Getworks")),
            "host": NAME + ".local", "mac": "", "ip": my_ip(), "stage": stage, "mode": "ASIC (Block Erupter)", "diag": err,
            "sub": "", "rep": "", "auth": "", "gen": 0, "rst": "", "err": err}

def si(v):
    for u, d in (("GH/s", 1e9), ("MH/s", 1e6), ("kH/s", 1e3)):
        if v >= d: return "%.2f %s" % (v / d, u)
    return "%d H/s" % v

TXT = {"en": ("Hashrate", "Shares ok / rejected", "Pool", "Status", "Uptime", "CPU temp"),
       "el": ("Ρυθμός hash", "Shares αποδεκτά / απορριφθέντα", "Pool", "Κατάσταση", "Χρόνος λειτουργίας", "Θερμοκρασία CPU")}

PAGE = """<!doctype html><meta charset=utf-8><meta name=viewport content="width=device-width,initial-scale=1"><title>CRSminer Pi</title>
<style>body{background:#0f1116;color:#e8e8ea;font:16px system-ui;margin:0;padding:16px}h1{font-size:18px}h1 b{color:#f7931a}
.c{background:#171a21;border:1px solid #262b36;border-radius:12px;padding:12px 14px;margin:10px 0}.c small{display:block;color:#8b93a4;font-size:12px;text-transform:uppercase}
.c span{font-size:22px;font-weight:650}.h span{color:#f7931a;font-size:32px}</style>
<h1><b>CRS</b>miner Pi &middot; %(host)s</h1>
<div class="c h"><small>%(l0)s</small><span>%(hr)s</span></div>
<div class=c><small>%(l1)s</small><span>%(acc)d / %(rej)d</span></div>
<div class=c><small>%(l3)s</small><span>%(stage)s</span> %(err)s</div>
<div class=c><small>%(l2)s</small><span>%(pool)s</span></div>
<div class=c><small>%(l4)s / %(l5)s</small><span>%(up)s &middot; %(temp)s &deg;C</span></div>
<script>setTimeout(()=>location.reload(),5000)</script>"""

class H(http.server.BaseHTTPRequestHandler):
    def log_message(self, *a): pass
    def do_GET(self):
        s = status()
        if self.path.startswith("/api/status"):
            body = json.dumps(s).encode(); ct = "application/json"
        elif self.path == "/":
            l = TXT.get(LANG, TXT["en"]); up = s["up"]
            body = (PAGE % {"host": s["host"], "hr": si(s["hr"]), "acc": s["acc"], "rej": s["rej"], "stage": s["stage"], "pool": s["pool"],
                            "up": "%dd %02dh %02dm" % (up // 86400, up % 86400 // 3600, up % 3600 // 60), "temp": s["temp"],
                            "err": "<br>" + s["err"] if s["err"] else "", "l0": l[0], "l1": l[1], "l2": l[2], "l3": l[3], "l4": l[4], "l5": l[5]}).encode()
            ct = "text/html; charset=utf-8"
        else:
            self.send_error(404); return
        self.send_response(200); self.send_header("Content-Type", ct); self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Content-Length", str(len(body))); self.end_headers(); self.wfile.write(body)

if __name__ == "__main__":
    http.server.ThreadingHTTPServer(("", 80), H).serve_forever()
