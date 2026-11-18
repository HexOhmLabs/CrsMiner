"""Offline hash checks and an end-to-end mock Stratum server (no real pool)."""
import hashlib
import json
import os
import socket
import struct
import threading
import time
import unittest
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from miner_core import (MiningEngine, DIFF1_TARGET, build_header_prefix,
                        parse_pool_url, pool_login, sha256d, swap_words_4, target_from_difficulty,
                        stratum_result_ok, pool_response_details, ESP_STRATUM_AGENT)
from config_and_hardware import default_settings, validate_settings
from fleet_api import LocalFleetServer


class CoreTests(unittest.TestCase):
    def test_known_bitcoin_genesis(self):
        header = bytes.fromhex(
            '01000000' + '00' * 32 +
            '3ba3edfd7a7b12b27ac72c3e67768f617fc81bc3888a51323a9fb8aa4b1e5e4a' +
            '29ab5f49' + 'ffff001d' + '1dac2b7c')
        self.assertEqual(len(header), 80)
        self.assertTrue(sha256d(header)[::-1].hex().startswith('000000000019d668'))

    def test_diff1(self):
        self.assertEqual(target_from_difficulty(1), DIFF1_TARGET)
        self.assertEqual(target_from_difficulty(2), DIFF1_TARGET // 2)
        self.assertEqual(target_from_difficulty(0.000001), min((1 << 256) - 1, DIFF1_TARGET * 1000000))
        self.assertRaises(ValueError, target_from_difficulty, 0)

    def test_endian_and_header(self):
        prevhash = bytes(range(32))
        self.assertEqual(swap_words_4(prevhash)[:8].hex(), '0302010007060504')
        job = {'job_id': '1', 'prevhash': '00' * 32,
               'coinb1': '010203', 'coinb2': '040506',
               'extra1': 'aabb', 'extra2_size': 2,
               'branches': [], 'version': '20000000',
               'nbits': '1d00ffff', 'ntime': '495fab29'}
        x = build_header_prefix(job, bytes.fromhex('0010'))
        self.assertEqual(x[:4].hex(), '00000020')
        self.assertEqual(x[36:68], sha256d(bytes.fromhex('010203aabb0010040506')))
        self.assertEqual(x[-8:].hex(), '29ab5f49ffff001d')

    def test_pool_login_auto_worker_and_no_double_suffix(self):
        s = dict(default_settings(), wallet='DGBaccount', worker='pc01')
        self.assertEqual(pool_login(s), 'DGBaccount.pc01')
        self.assertEqual(s['wallet'], 'DGBaccount')  # stored field unchanged
        s['wallet'] = 'DGBaccount.manual'
        self.assertEqual(pool_login(s), 'DGBaccount.manual')
        s['wallet'] = 'DGBaccount.pc01'
        self.assertEqual(pool_login(s), 'DGBaccount.pc01')
        s['wallet'] = 'account.manual.extra'
        self.assertEqual(pool_login(s), 'account.manual.extra')
        s['wallet'] = 'DGBaccount'
        s['worker'] = 'pc02'
        self.assertEqual(pool_login(s), 'DGBaccount.pc02')
        s['wallet'] = ''
        s['pool'] = 'localhost:3333'
        with self.assertRaises(ValueError):
            validate_settings(s)

    def test_password_default_blank(self):
        self.assertEqual(default_settings()['password'], '')

    def test_esp_login_result_variants_and_diagnostics(self):
        for good in (True, 1, 'true', 'TRUE', '1'):
            self.assertTrue(stratum_result_ok(good), repr(good))
        for bad in (False, 0, None, 'false', 'no', [], {}):
            self.assertFalse(stratum_result_ok(bad), repr(bad))
        self.assertIn('result=False (bool); error=None',
                      pool_response_details({'result': False, 'error': None}))
        self.assertIn('invalid user',
                      pool_response_details({'result': False, 'error': [24, 'invalid user', None]}))

    def test_stratum_urls(self):
        self.assertEqual(parse_pool_url('pool.example:3333'), ('pool.example', 3333, False))
        self.assertEqual(parse_pool_url('stratum+ssl://pool.example:443'), ('pool.example', 443, True))
        self.assertRaises(ValueError, parse_pool_url, 'https://example.com')


class FakePool(threading.Thread):
    """Validates the miner's submitted shares against independent SHA-256d calculations."""
    def __init__(self, expected_username=None, expected_password=None, auth_result=True):
        super().__init__(daemon=True)
        self.expected_username = expected_username
        self.expected_password = expected_password
        self.auth_result = auth_result
        self.received_methods = []
        self.authorized = False
        self.observed_auth = None
        self.observed_submit_user = None
        self.listen_sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.listen_sock.bind(('127.0.0.1', 0))
        self.listen_sock.listen(1)
        self.listen_sock.settimeout(15)
        self.port = self.listen_sock.getsockname()[1]
        self.accepted = threading.Event()
        self.errors = []
        self.received_shares = 0
        self.job = {
            'job_id': 'mock-job-01', 'prevhash': '12' * 32,
            'coinb1': '01020304', 'coinb2': '05060708',
            'branches': ['11' * 32], 'version': '20000000',
            'nbits': '1d00ffff', 'ntime': '495fab29',
            'extra1': 'aabbccdd', 'extra2_size': 4,
        }
        self.difficulty = 0.000002

    def run(self):
        try:
            connection, _ = self.listen_sock.accept()
            with connection:
                connection.settimeout(12)
                rx = connection.makefile('rb')
                tx = connection.makefile('wb')

                def write(ob):
                    tx.write(json.dumps(ob).encode() + b'\n')
                    tx.flush()

                while not self.accepted.is_set():
                    data = rx.readline()
                    if not data:
                        return
                    msg = json.loads(data)
                    method = msg.get('method')
                    self.received_methods.append(method)
                    if method == 'mining.suggest_difficulty':
                        if not self.authorized:
                            self.errors.append('Difficulty suggested before authorization: ESP order violated')
                            return
                        continue
                    if method == 'mining.subscribe':
                        if msg.get('params') != [ESP_STRATUM_AGENT]:
                            self.errors.append('ESP subscription agent was not used')
                            return
                        write({'id': msg['id'], 'result': [[['mining.notify', 'mock-sub']], 'aabbccdd', 4], 'error': None})
                    elif method == 'mining.authorize':
                        self.observed_auth = list(msg.get('params') or [])
                        if self.expected_username is not None and self.observed_auth[0] != self.expected_username:
                            self.errors.append('Unexpected authorization username')
                            return
                        if self.expected_password is not None and self.observed_auth[1] != self.expected_password:
                            self.errors.append('Unexpected authorization password')
                            return
                        write({'id': msg['id'], 'result': self.auth_result, 'error': None})
                        self.authorized = True
                        write({'id': None, 'method': 'mining.set_difficulty', 'params': [self.difficulty]})
                        p = self.job
                        write({'id': None, 'method': 'mining.notify', 'params': [
                            p['job_id'], p['prevhash'], p['coinb1'], p['coinb2'], p['branches'],
                            p['version'], p['nbits'], p['ntime'], True]})
                    elif method == 'mining.submit':
                        user, job_id, ex2, ntime, nonce = msg['params']
                        self.observed_submit_user = user
                        if self.expected_username is not None and user != self.expected_username:
                            self.errors.append('Unexpected submit username')
                            return
                        self.received_shares += 1
                        if job_id != self.job['job_id'] or ntime != self.job['ntime']:
                            self.errors.append('Submitted unknown job or ntime')
                            write({'id': msg['id'], 'result': False, 'error': [21, 'Bad job', None]})
                            continue
                        prefix = build_header_prefix(self.job, bytes.fromhex(ex2))
                        digest = hashlib.sha256(hashlib.sha256(prefix + struct.pack('<I', int(nonce, 16))).digest()).digest()
                        if int.from_bytes(digest, 'little') <= target_from_difficulty(self.difficulty):
                            write({'id': msg['id'], 'result': True, 'error': None})
                            self.accepted.set()
                        else:
                            self.errors.append('Invalid hash submitted')
                            write({'id': msg['id'], 'result': False, 'error': [23, 'Bad hash', None]})
        except Exception as exc:
            self.errors.append(str(exc))
        finally:
            self.listen_sock.close()


class IntegratedTests(unittest.TestCase):
    def test_local_pool_accepts_valid_share(self):
        pool = FakePool(expected_username='demoaccount.test', expected_password='')
        pool.start()
        settings = default_settings()
        settings.update(name='pc01', pool=f'127.0.0.1:{pool.port}', wallet='demoaccount',
                        worker='test', threads=1, effort=100, fleet=False)
        engine = MiningEngine()
        try:
            engine.start(settings)
            deadline = time.monotonic() + 20
            while time.monotonic() < deadline and engine.snapshot()['acc'] == 0 and not pool.errors:
                time.sleep(0.1)
            self.assertFalse(pool.errors, pool.errors)
            self.assertGreaterEqual(pool.received_shares, 1, 'No candidate shares submitted')
            self.assertGreaterEqual(engine.snapshot()['acc'], 1, 'No share acceptance received')
            self.assertTrue(pool.accepted.is_set())
            self.assertEqual(pool.observed_auth, ['demoaccount.test', ''])
            self.assertEqual(pool.observed_submit_user, 'demoaccount.test')
        finally:
            engine.stop()

    def test_local_pool_custom_login_password(self):
        pool = FakePool(expected_username='exact.login', expected_password='Ab$ 123!')
        pool.start()
        settings = default_settings()
        settings.update(name='pc01', pool=f'127.0.0.1:{pool.port}', wallet='exact.login',
                        worker='unused_worker',
                        password='Ab$ 123!', threads=1, effort=100, fleet=False)
        validate_settings(settings)
        engine = MiningEngine()
        try:
            engine.start(settings)
            deadline = time.monotonic() + 20
            while time.monotonic() < deadline and engine.snapshot()['acc'] == 0 and not pool.errors:
                time.sleep(0.1)
            self.assertFalse(pool.errors, pool.errors)
            self.assertEqual(pool.observed_auth, ['exact.login', 'Ab$ 123!'])
            self.assertEqual(pool.observed_submit_user, 'exact.login')
            self.assertGreaterEqual(engine.snapshot()['acc'], 1)
        finally:
            engine.stop()

    def test_esp_connection_order_and_integer_authorization_response(self):
        """Strict pool: subscribe with ESP agent, authorize, THEN suggest difficulty."""
        pool = FakePool(expected_username='no.period.insertion',
                        expected_password='custom-password-123', auth_result=1)
        pool.start()
        settings = default_settings()
        settings.update(name='pc02', pool=f'127.0.0.1:{pool.port}', wallet='no.period.insertion',
                        worker='never_appended',
                        password='custom-password-123', suggested_difficulty='0.001',
                        threads=1, effort=100, fleet=False)
        engine = MiningEngine()
        logs = []
        engine.on_log = logs.append
        try:
            engine.start(settings)
            deadline = time.monotonic() + 20
            while time.monotonic() < deadline and engine.snapshot()['acc'] == 0 and not pool.errors:
                time.sleep(0.1)
            self.assertFalse(pool.errors, pool.errors)
            self.assertEqual(pool.observed_auth, ['no.period.insertion', 'custom-password-123'])
            self.assertEqual(pool.observed_submit_user, 'no.period.insertion')
            self.assertGreaterEqual(engine.snapshot()['acc'], 1)
            self.assertIn('mining.suggest_difficulty', pool.received_methods)
            self.assertLess(pool.received_methods.index('mining.authorize'),
                            pool.received_methods.index('mining.suggest_difficulty'))
            self.assertFalse(any('custom-password-123' in ln for ln in logs))
            self.assertFalse(any('no.period.insertion' in ln for ln in logs))
        finally:
            engine.stop()

    def test_fleet_api_format(self):
        # A local port binding test. No network scanning or public-pool calls.
        engine = MiningEngine()
        engine.config = dict(default_settings(), name='pc23')
        api = LocalFleetServer(engine, lambda: engine.config, port=0)
        api.start()
        try:
            if api.error:
                self.skipTest(api.error)
            import urllib.request
            data = json.load(urllib.request.urlopen('http://127.0.0.1:%s/api/status' % api.httpd.server_address[1], timeout=3))
            self.assertEqual(data['host'], 'crsminer-pc23.local')
            self.assertTrue(all(k in data for k in ('hr', 'acc', 'rej', 'ok')))
            self.assertEqual(data['platform'], 'windows')
        finally:
            api.stop()


if __name__ == '__main__':
    unittest.main()
