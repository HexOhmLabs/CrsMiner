#!/usr/bin/env python3
"""CRS Mining Web — read-only localhost bridge for ESP and Android CRSminers.
Standard-library Python 3.9+. No changes to either miner and no extra packages.
Only searches private LAN IPv4 addresses and only fetches /api/status,/api/config.
"""
from __future__ import annotations

import concurrent.futures
import http.client
import ipaddress
import json
import os
import pathlib
import re
import subprocess
import socket
import threading
import time
import urllib.error
import urllib.parse
import urllib.request
import webbrowser
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

HERE = pathlib.Path(__file__).resolve().parent
PORT = 8765
# Local miner traffic must not be routed through system or environment HTTP proxies.
_LOCAL_HTTP = urllib.request.build_opener(urllib.request.ProxyHandler({}))
PERSIST = HERE / 'known_miners.json'
_LOCK = threading.RLock()
_MINERS: dict[str, dict] = {}
_SCAN = {'running': False, 'scanned': 0, 'total': 0, 'last': 'Safe mode: monitoring saved miners only. Full scan requires a click.', 'subnets': []}
_STOP = threading.Event()
# Safe defaults: at most two simultaneous LAN operations, one per miner host.
SCAN_WORKERS = 2
POLL_SECONDS = 12.0
CONNECT_TIMEOUT = 0.30
STATUS_TIMEOUT = 1.55
_IO_SLOTS = threading.BoundedSemaphore(2)
_HOST_LOCKS: dict[str, threading.RLock] = {}
_NEXT_POLL: dict[str, float] = {}
_LAST_SCAN_START = 0.0
_SCAN_COOLDOWN_SECONDS = 120.0


def host_lock(target: str) -> threading.RLock:
    host, _ = split_target(target)
    with _LOCK:
        return _HOST_LOCKS.setdefault(host, threading.RLock())



def allowed_ip(ip: str) -> bool:
    try:
        a = ipaddress.ip_address(ip)
        return a.version == 4 and not (a.is_loopback or a.is_multicast or a.is_unspecified or a.is_link_local or a.is_reserved) and (
            a in ipaddress.ip_network('10.0.0.0/8') or
            a in ipaddress.ip_network('172.16.0.0/12') or
            a in ipaddress.ip_network('192.168.0.0/16')
        )
    except ValueError:
        return False


def split_target(target: str) -> tuple[str, int]:
    if target.endswith(':8080'):
        return target[:-5], 8080
    return target, 80


def valid_target(target: str) -> bool:
    host, port = split_target(target)
    return port in (80, 8080) and allowed_ip(host)


def normalized_local_ip(value: str) -> str:
    """Accept RFC1918 IP or crsminer-XXXX.local name, never arbitrary URL/host."""
    value = value.strip()
    if len(value) > 120:
        raise ValueError('Invalid address')
    if value.lower().startswith(('http://', 'https://')):
        u = urllib.parse.urlsplit(value)
        if u.scheme != 'http' or u.port not in (None, 80, 8080) or u.username or u.password:
            raise ValueError('Enter a local HTTP address (port 80 or 8080).')
        value = (u.hostname or '') + (':8080' if u.port == 8080 else '')
    value = value.strip().strip('/').lower()
    if value.endswith(':80') and allowed_ip(value[:-3]):
        return value[:-3]
    if valid_target(value):
        return value
    if value.startswith('crsminer-') and value.endswith('.local') and len(value) <= 45:
        name = value[9:-6]
        if not name or not all(c.isalnum() or c == '-' for c in name):
            raise ValueError('Invalid CRSminer hostname')
        try:
            resolved = socket.gethostbyname(value)
        except OSError as e:
            raise ValueError('Hostname could not be resolved; try its IP address.') from e
        if allowed_ip(resolved):
            return resolved
    raise ValueError('Use a private LAN IP (192.168.1.42 or 192.168.1.42:8080) or crsminer-XXXX.local')


def json_from_miner(ip: str, endpoint: str, timeout=STATUS_TIMEOUT) -> dict | None:
    host, port = split_target(ip)
    if not allowed_ip(host) or endpoint not in ('/api/status', '/api/config'):
        return None
    req = urllib.request.Request('http://' + host + (':8080' if port == 8080 else '') + endpoint,
            headers={'Accept': 'application/json', 'User-Agent': 'CRS-Mining-Web/1.6.1'})
    try:
        with host_lock(ip), _IO_SLOTS:
            with _LOCAL_HTTP.open(req, timeout=timeout) as response:
                if response.status != 200:
                    return None
                data = response.read(32769)
                if len(data) > 32768:
                    return None
                payload = json.loads(data)
                return payload if isinstance(payload, dict) else None
    except (OSError, ValueError, urllib.error.URLError):
        return None


def gentle_discover(target: str) -> dict | None:
    """Single GET /api/status per endpoint, with separate connect/read limits.

    The earlier bare TCP preflight was followed immediately by a second HTTP
    connection. Some small single-client ESP servers can still be handling the
    just-closed first connection, leading to a false negative on the second.
    This variant uses exactly ONE connection to a host:port, retains the global
    two-connection cap and per-host serialization, and never requests config.
    """
    host, port = split_target(target)
    if not valid_target(target):
        return None
    connection = None
    with host_lock(target), _IO_SLOTS:
        try:
            connection = http.client.HTTPConnection(host, port, timeout=CONNECT_TIMEOUT)
            connection.connect()
            # A successful TCP connect should not inherit the shorter connect
            # deadline while receiving ESP's potentially delayed HTTP response.
            connection.sock.settimeout(STATUS_TIMEOUT)
            connection.request('GET', '/api/status', headers={
                'Accept': 'application/json',
                'User-Agent': 'CRS-Mining-Web/1.6.1',
                'Connection': 'close',
            })
            response = connection.getresponse()
            if response.status != 200:
                return None
            data = response.read(32769)
            if len(data) > 32768:
                return None
            payload = json.loads(data)
            return payload if isinstance(payload, dict) else None
        except (OSError, ValueError, http.client.HTTPException):
            return None
        finally:
            if connection is not None:
                connection.close()


def valid_status(data: dict | None) -> bool:
    if not isinstance(data, dict):
        return False
    host = str(data.get('host', '')).lower()
    return (host.startswith('crsminer-') and host.endswith('.local') and
            all(k in data for k in ('hr', 'acc', 'rej', 'ok')))



def _probe_endpoint_impl(target: str) -> dict:
    """Report connectivity and API compatibility without changing a miner.

    Only explicit, validated RFC1918 host:80/8080 targets are accepted.
    This exposes error categories, not wallet credentials or full JSON.
    """
    host, port = split_target(target)
    result = {'target': target, 'ip': host, 'port': port,
              'url': f'http://{host}:{port}/api/status', 'reachable': False,
              'valid_crsminer': False, 'message': ''}
    req = urllib.request.Request(result['url'],
        headers={'Accept': 'application/json', 'User-Agent': 'CRS-Mining-Web/1.6.1'})
    try:
        with _LOCAL_HTTP.open(req, timeout=3.5) as response:
            code = response.status
            result['http_status'] = code
            raw = response.read(32769)
            if len(raw) > 32768:
                result['message'] = 'HTTP responded, but the body exceeded the 32 KB limit.'
                return result
            try:
                payload = json.loads(raw)
            except (ValueError, UnicodeDecodeError) as e:
                result['message'] = f'HTTP {code}, but the reply is not valid JSON: {e}'
                return result
            result['reachable'] = True
            if not isinstance(payload, dict):
                result['message'] = 'HTTP reachable, but the JSON is not an object.'
            elif valid_status(payload):
                result['valid_crsminer'] = True
                result['message'] = 'CRSminer detected. Status API recognized.'
                result['miner_host'] = str(payload.get('host', ''))[:80]
                result['hashrate'] = payload.get('hr', 0)
            else:
                result['message'] = ('HTTP reachable, but response is not a recognized CRSminer. '
                                     'Fields: ' + ', '.join(sorted(str(k) for k in payload)[:15]))
    except urllib.error.HTTPError as e:
        result['http_status'] = e.code
        result['message'] = f'HTTP {e.code}: service replied, but /api/status was not accepted.'
    except Exception as e:
        result['message'] = f'{type(e).__name__}: {str(e)[:180]}'
    return result


def probe_endpoint(target: str) -> dict:
    """An explicitly requested one-off test, no test during full discovery."""
    with _LOCK:
        busy = _SCAN['running']
    if busy:
        host, port = split_target(target)
        return {'target': target, 'ip': host, 'port': port,
                'reachable': False, 'valid_crsminer': False,
                'message': 'Safe scan is running. Try the test after discovery finishes.'}
    with host_lock(target), _IO_SLOTS:
        return _probe_endpoint_impl(target)


def write_known_ips():
    with _LOCK:
        known = sorted(_MINERS)
    try:
        temp = PERSIST.with_suffix('.tmp')
        temp.write_text(json.dumps({'ips': known}, indent=2), encoding='utf-8')
        temp.replace(PERSIST)
    except OSError:
        pass


def load_known_ips():
    try:
        ips = json.loads(PERSIST.read_text(encoding='utf-8')).get('ips', [])
        for ip in ips:
            if isinstance(ip, str) and valid_target(ip):
                with _LOCK:
                    _MINERS[ip] = new_miner(ip)
    except (OSError, ValueError, TypeError, AttributeError):
        pass


def new_miner(ip):
    return {'ip': ip, 'host': ip, 'worker': '', 'pool': '', 'wallet': '', 'reachable': False,
            'hr': 0, 'acc': 0, 'rej': 0, 'ok': False, 'diag': '', 'err': '', 'stage': '',
            'best': 0, 'pdiff': 0, 'up': 0, 'rssi': 0, 'temp': 0, 'jobs': 0,
            'mac': '', 'mode': '', 'heap': 0, 'cores': [], 'misses': 0, 'history': [],
            'last_seen': 0, 'config_time': 0, 'platform': 'unknown', 'http_port': split_target(ip)[1]}


def poll(ip: str, fresh=False, status=None) -> bool:
    # An incoming scan status is reused, never fetched twice. No automatic
    # /api/config requests: configuration is loaded only on details demand.
    if status is None:
        status = json_from_miner(ip, '/api/status', timeout=STATUS_TIMEOUT)
    if not valid_status(status):
        with _LOCK:
            if ip in _MINERS:
                m = _MINERS[ip]
                m['misses'] += 1
                # A transient timeout must not turn a normally mining ESP red.
                if m['misses'] >= 3:
                    m['reachable'] = False
        return False
    now = time.time()
    with _LOCK:
        if ip not in _MINERS:
            _MINERS[ip] = new_miner(ip)
        m = _MINERS[ip]
        for f in ('host', 'hr', 'acc', 'rej', 'ok', 'diag', 'err', 'stage',
                  'best', 'pdiff', 'up', 'rssi', 'temp', 'jobs', 'mac',
                  'mode', 'heap', 'cores', 'platform', 'http_port'):
            if f in status:
                m[f] = status[f]
        if 'platform' not in status:
            m['platform'] = 'android' if split_target(ip)[1] == 8080 else 'esp'
        m['reachable'] = True
        m['misses'] = 0
        m['last_seen'] = now
        try:
            rate = max(0.0, float(status.get('hr') or 0))
        except (TypeError, ValueError):
            rate = 0.0
        m['history'].append(rate)
        m['history'] = m['history'][-40:]
        # pool is exposed by both /api/status formats.
        m['pool'] = str(status.get('pool') or m['pool'])[:200]
    return True


def load_details(target: str) -> dict:
    """Fetch /api/config only when a user opens a miner's details.

    Cached on future details requests (explicit force=True refreshes it).
    Never include passwords; do not persist wallet/configuration to disk.
    """
    with _LOCK:
        m = _MINERS.get(target)
        if m is None:
            return {'ok': False, 'error': 'Miner not saved'}
        if _SCAN['running']:
            return {'ok': False, 'error': 'Full scan in progress. Try details after it finishes.'}
        if m.get('config_time', 0) > 0 and time.time() - m['config_time'] < 60:
            return {'ok': True, 'cached': True}
    config = json_from_miner(target, '/api/config', timeout=STATUS_TIMEOUT)
    if not isinstance(config, dict):
        return {'ok': False, 'error': 'Miner configuration unavailable. Status monitoring is unaffected.'}
    with _LOCK:
        m = _MINERS.get(target)
        if m is None:
            return {'ok': False, 'error': 'Miner was removed'}
        m['worker'] = str(config.get('worker') or '')[:120]
        m['wallet'] = str(config.get('addr') or '')[:200]
        m['pool'] = str(config.get('pool') or m['pool'])[:200]
        m['config_time'] = time.time()
    return {'ok': True, 'cached': False}


def windows_interface_ips() -> list[str]:
    """Additional Windows LAN detection when hostname DNS points to a VPN/virtual adapter.

    ipconfig is built into Windows. We match IPv4 values rather than translated
    labels, so English and other Windows display languages are supported.
    """
    if os.name != 'nt':
        return []
    try:
        result = subprocess.run(['ipconfig'], stdout=subprocess.PIPE,
                                stderr=subprocess.DEVNULL, timeout=6, check=False)
        output = result.stdout.decode(errors='replace')
    except (OSError, subprocess.TimeoutExpired):
        return []
    # Only interface IPv4-address lines. The old broad regex also collected
    # gateway and DNS IPs, accidentally expanding the scan to unrelated LANs.
    values = []
    for line in output.splitlines():
        if 'ipv4' not in line.lower():
            continue
        values.extend(re.findall(r'(?<![\d.])(?:\d{1,3}\.){3}\d{1,3}(?![\d.])', line))
    return list(dict.fromkeys(ip for ip in values if allowed_ip(ip)))


def local_private_ips() -> list[str]:
    # The default route is tried first; adapter discovery fills in LANs obscured
    # by VPNs or Windows hostname resolution.
    candidates = []
    try:
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as s:
            s.connect(('1.1.1.1', 80))  # Route query; does not transmit a packet.
            candidates.append(s.getsockname()[0])
    except OSError:
        pass
    candidates.extend(windows_interface_ips())
    try:
        for result in socket.getaddrinfo(socket.gethostname(), None,
                                         socket.AF_INET, socket.SOCK_STREAM):
            candidates.append(result[4][0])
    except OSError:
        pass
    return list(dict.fromkeys(ip for ip in candidates if allowed_ip(ip)))


def local_subnets() -> list[ipaddress.IPv4Network]:
    # Discover private IPv4 /24 segments, prioritizing the default route and
    # Windows adapters. Explicitly added miner IPs are always scanned even if
    # the monitor PC is on a different subnet.
    seen = []
    for ip in local_private_ips():
        net = ipaddress.ip_network(ip + '/24', strict=False)
        if net not in seen:
            seen.append(net)
    # Prefer the subnet(s) containing already saved miners, then the PC's
    # primary route. Scan no more than three /24s, not eight (which could
    # include VPNs and unrelated virtual adapters).
    with _LOCK:
        saved_nets = {ipaddress.ip_network(split_target(t)[0] + '/24', strict=False)
                      for t in _MINERS if valid_target(t)}
    seen.sort(key=lambda net: net not in saved_nets)
    return seen[:3]


def scan_worker():
    """Explicit, low-impact scan; monitoring traffic pauses while it runs."""
    global _LAST_SCAN_START
    try:
        nets = local_subnets()
        with _LOCK:
            known = [t for t in _MINERS if valid_target(t)]
            _SCAN['subnets'] = [str(n) for n in nets]

        # Copy the Android monitor's preference for reachable local addresses,
        # but keep a bounded /24 per adapter (same scope as v1.4).
        hosts = set()
        for net in nets:
            hosts.update(str(ip) for ip in net.hosts() if allowed_ip(str(ip)))
        hosts.update(split_target(t)[0] for t in known)
        hosts = sorted(hosts, key=ipaddress.IPv4Address)
        known_by_host = {}
        for t in known:
            known_by_host.setdefault(split_target(t)[0], set()).add(t)
        hosts.sort(key=lambda host: host not in known_by_host)
        with _LOCK:
            _SCAN.update({'total': len(hosts), 'scanned': 0, 'last': 'Safe scan: 2 connections maximum'})
        if not hosts:
            with _LOCK:
                _SCAN['last'] = 'No private LAN detected. Use + Add miner with a known local address.'
            return

        def probe_host(host):
            # Saved miners: only the known service/port. Unknown miners:
            # discover ESP :80 and Android :8080 independently.
            targets = sorted(known_by_host.get(host, [])) or [host, host + ':8080']
            for target in targets:
                if _STOP.is_set():
                    break
                status = gentle_discover(target)
                if valid_status(status):
                    poll(target, status=status)
                # Discovery never marks a known miner offline due to one miss.
            with _LOCK:
                _SCAN['scanned'] += 1
            # A brief gap reduces back-to-back requests toward Wi-Fi clients.
            _STOP.wait(0.12)

        with concurrent.futures.ThreadPoolExecutor(max_workers=SCAN_WORKERS,
                thread_name_prefix='gentle-discovery') as executor:
            for _ in executor.map(probe_host, hosts):
                if _STOP.is_set():
                    break
        write_known_ips()
        with _LOCK:
            count = len(_MINERS)
            online = sum(1 for m in _MINERS.values() if m['reachable'])
            _SCAN['last'] = (f'Safe scan finished: {online} responding, {count} saved; '
                             f'{len(hosts)} IPs checked, maximum {SCAN_WORKERS} connections. '
                             'Known miners are monitored without further full scans.')
    except Exception as e:
        with _LOCK:
            _SCAN['last'] = 'Safe scan failed: ' + str(e)[:180]
    finally:
        with _LOCK:
            _SCAN['running'] = False
            _LAST_SCAN_START = time.monotonic()


def start_scan() -> bool:
    global _LAST_SCAN_START
    now = time.monotonic()
    with _LOCK:
        if _SCAN['running']:
            return False
        remaining = _SCAN_COOLDOWN_SECONDS - (now - _LAST_SCAN_START)
        if _LAST_SCAN_START and remaining > 0:
            _SCAN['last'] = f'Safe scan cooldown: try again in {int(remaining) + 1}s.'
            return False
        _SCAN.update({'running': True, 'scanned': 0, 'total': 0,
                      'last': 'Checking known miners, then local LAN (manual scan).'})
        _LAST_SCAN_START = now
    threading.Thread(target=scan_worker, name='gentle-discovery', daemon=True).start()
    return True


def poll_loop():
    """Single worker: stagger known miner requests, never poll during scan.

    At most one status request here, with exponential backoff when offline.
    No automatic configuration reads and no automatic full-network scan.
    """
    while not _STOP.wait(0.35):
        with _LOCK:
            if _SCAN['running']:
                continue
            candidates = list(_MINERS)
            for t in list(_NEXT_POLL):
                if t not in _MINERS:
                    _NEXT_POLL.pop(t, None)
        if not candidates:
            continue
        now = time.monotonic()
        due = sorted(((_NEXT_POLL.get(ip, 0.0), ip) for ip in candidates))
        if not due or due[0][0] > now:
            continue
        _, target = due[0]
        # Reserve before network IO to avoid immediate repeats.
        with _LOCK:
            _NEXT_POLL[target] = now + POLL_SECONDS
        ok = poll(target)
        with _LOCK:
            if target in _MINERS and not ok:
                misses = _MINERS[target]['misses']
                # Failed devices are checked less frequently, not hammered.
                _NEXT_POLL[target] = time.monotonic() + min(120.0, POLL_SECONDS * (2 ** min(misses, 3)))
        _STOP.wait(0.50)


class Handler(BaseHTTPRequestHandler):
    def log_message(self, fmt, *args):
        if not self.path.startswith('/api/miners'):
            print('HTTP:', fmt % args)

    def reply(self, status, obj):
        data = json.dumps(obj, ensure_ascii=False).encode('utf-8')
        self.send_response(status)
        self.send_header('Content-Type', 'application/json; charset=utf-8')
        self.send_header('Content-Length', str(len(data)))
        self.send_header('Cache-Control', 'no-store')
        self.send_header('X-Content-Type-Options', 'nosniff')
        self.end_headers()
        self.wfile.write(data)

    def security(self):
        # Only let this local website call the local API.
        host = self.headers.get('Host', '').split(':')[0].lower()
        if host not in ('localhost', '127.0.0.1'):
            self.reply(403, {'error': 'Localhost only'})
            return False
        origin = self.headers.get('Origin', '')
        if origin and origin not in (f'http://localhost:{PORT}', f'http://127.0.0.1:{PORT}'):
            self.reply(403, {'error': 'Origin denied'})
            return False
        return True

    def do_GET(self):
        if not self.security():
            return
        path = urllib.parse.urlsplit(self.path).path
        if path in ('/', '/index.html', '/CRS_Mining_Web.html'):
            data = (HERE / 'CRS_Mining_Web.html').read_bytes()
            self.send_response(200)
            self.send_header('Content-Type', 'text/html; charset=utf-8')
            self.send_header('Content-Length', str(len(data)))
            self.send_header('Cache-Control', 'no-store')
            self.send_header('X-Content-Type-Options', 'nosniff')
            self.end_headers()
            self.wfile.write(data)
        elif path == '/api/miners':
            with _LOCK:
                miners = [dict(m) for m in _MINERS.values()]
                scan = dict(_SCAN)
            self.reply(200, {'miners': miners, 'scan': scan, 'version': '1.6.1', 'refresh_seconds': 5, 'poll_seconds': POLL_SECONDS, 'safe_mode': True})
        elif path == '/api/health':
            self.reply(200, {'ok': True, 'name': 'CRS Mining Web', 'version': '1.6.1'})
        else:
            self.reply(404, {'error': 'Not found'})

    def do_POST(self):
        if not self.security():
            return
        path = urllib.parse.urlsplit(self.path).path
        if path not in ('/api/scan', '/api/add', '/api/remove', '/api/probe', '/api/details'):
            return self.reply(404, {'error': 'Not found'})
        try:
            size = int(self.headers.get('Content-Length', '0'))
        except ValueError:
            return self.reply(400, {'error': 'Invalid request'})
        if size < 0 or size > 512:
            return self.reply(413, {'error': 'Request too large'})
        try:
            data = json.loads(self.rfile.read(size)) if size else {}
        except (ValueError, UnicodeDecodeError):
            return self.reply(400, {'error': 'Invalid JSON'})
        if path == '/api/scan':
            return self.reply(200, {'started': start_scan()})
        try:
            ip = normalized_local_ip(str(data.get('address', '')))
        except ValueError as e:
            return self.reply(400, {'error': str(e)})
        if path == '/api/details':
            return self.reply(200, load_details(ip))
        if path == '/api/probe':
            # This is a diagnostic call, not a miner control action.
            # It does not add or alter any device.
            return self.reply(200, probe_endpoint(ip))
        if path == '/api/add':
            with _LOCK:
                if ip not in _MINERS:
                    _MINERS[ip] = new_miner(ip)
            write_known_ips()
            with _LOCK:
                _NEXT_POLL[ip] = 0.0
            return self.reply(200, {'ok': True, 'ip': ip})
        with _LOCK:
            removed = _MINERS.pop(ip, None) is not None
        with _LOCK:
            _NEXT_POLL.pop(ip, None)
        write_known_ips()
        return self.reply(200, {'ok': True, 'removed': removed})


def main():
    load_known_ips()
    try:
        server = ThreadingHTTPServer(('127.0.0.1', PORT), Handler)
    except OSError as e:
        print(f'Cannot start local server on 127.0.0.1:{PORT}: {e}')
        raise SystemExit(1)
    server.daemon_threads = True
    print('CRS Mining Web — Version 1.6.1 Safe Monitoring · 2026 by HexOhm')
    print('Read-only monitoring for ESP and Android CRSminers. No miner changes.')
    print(f'Open: http://127.0.0.1:{PORT}')
    print('Keep this window open. Press Ctrl+C to stop.')
    print('For a direct LAN diagnostic, use + Add miner > Test connection.')
    threading.Thread(target=poll_loop, daemon=True).start()
    print('Safe mode: no automatic network scan. Press Scan network only when needed.')
    threading.Timer(1, lambda: webbrowser.open(f'http://127.0.0.1:{PORT}/')).start()
    try:
        server.serve_forever(poll_interval=0.3)
    except KeyboardInterrupt:
        print('\nStopping CRS Mining Web.')
    finally:
        _STOP.set()
        server.server_close()
        # No global worker pool: all scan threads terminate with their scan.


if __name__ == '__main__':
    main()
