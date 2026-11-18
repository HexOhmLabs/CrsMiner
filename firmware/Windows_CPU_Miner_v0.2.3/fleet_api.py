"""Read-only, CRSminer-compatible port-8080 LAN status endpoint."""
from __future__ import annotations

import ipaddress
import json
import threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer


class LocalFleetServer:
    def __init__(self, engine, config_getter, port=8080):
        self.engine = engine
        self.config_getter = config_getter
        self.port = port
        self.httpd = None
        self.thread = None
        self.error = ''

    def start(self):
        if self.httpd is not None:
            return
        engine = self.engine
        get_settings = self.config_getter

        class Handler(BaseHTTPRequestHandler):
            protocol_version = 'HTTP/1.1'

            def log_message(self, fmt, *args):
                return

            def do_GET(self):
                try:
                    ip = ipaddress.ip_address(self.client_address[0])
                    if not (ip.is_private or ip.is_loopback):
                        return self.send_json({'error': 'LAN only'}, 403)
                except ValueError:
                    return self.send_json({'error': 'Invalid address'}, 403)
                if self.path == '/api/status':
                    return self.send_json(engine.snapshot())
                if self.path == '/api/config':
                    cfg = get_settings()
                    return self.send_json({'name': cfg.get('name', ''),
                        'pool': cfg.get('pool', ''), 'addr': cfg.get('wallet', ''),
                        'worker': cfg.get('worker', ''), 'sugg': cfg.get('suggested_difficulty', ''),
                        'platform': 'windows', 'mode': 'CPU SHA256d'})
                return self.send_json({'error': 'Unknown endpoint'}, 404)

            def send_json(self, obj, status=200):
                body = json.dumps(obj, separators=(',', ':'), ensure_ascii=False).encode('utf-8')
                self.send_response(status)
                self.send_header('Content-Type', 'application/json; charset=utf-8')
                self.send_header('Cache-Control', 'no-store')
                self.send_header('Access-Control-Allow-Origin', '*')
                self.send_header('Content-Length', str(len(body)))
                self.send_header('Connection', 'close')
                self.end_headers()
                try:
                    self.wfile.write(body)
                except (BrokenPipeError, ConnectionResetError):
                    pass
                self.close_connection = True

        try:
            self.httpd = ThreadingHTTPServer(('0.0.0.0', self.port), Handler)
            self.httpd.daemon_threads = True
            self.httpd.timeout = 0.3
            self.thread = threading.Thread(target=self.httpd.serve_forever, daemon=True, name='FleetAPI')
            self.thread.start()
            self.error = ''
        except OSError as exc:
            self.error = f'Fleet API unavailable on port 8080: {exc}'
            self.httpd = None
            self.thread = None

    def stop(self):
        if self.httpd:
            self.httpd.shutdown()
            self.httpd.server_close()
            self.httpd = None
            self.thread = None
