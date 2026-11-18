"""HexOhm CRSminer PC — Stratum V1 SHA-256d CPU miner.

Independent worker processes spread genuine SHA-256d nonce searches over the CPU
cores, using the bundled native engine (SHA-NI / AVX2 / scalar, chosen at run
time) with a pure-Python hashlib fallback. All pool traffic stays in the
supervisor process. No fee, no donation hash-rate, no hidden endpoint.
"""
from __future__ import annotations

import hashlib
import json
import multiprocessing as mp
import os
import queue
import socket
import ssl
import struct
import threading
import time
from decimal import Decimal, InvalidOperation, localcontext
from urllib.parse import urlparse
from native_hash import get_scanner
from diagnostic_log import DiagnosticLog

DIFF1_TARGET = 0xFFFF << 208
MAX_TARGET = (1 << 256) - 1
BATCH_SIZE = 2048            # pure-Python fallback batch (nonces per loop)
NATIVE_BATCH_MIN = 1 << 12    # native batches are auto-sized to ~NATIVE_BATCH_SECONDS of work
NATIVE_BATCH_MAX = 1 << 26
NATIVE_BATCH_SECONDS = 0.03   # also the worst-case delay before a worker notices a new job
MAX_LINE_SIZE = 1024 * 1024
# Use the subscription identification from the working classic ESP32 firmware.
ESP_STRATUM_AGENT = 'CRSminer/1.0'


def stratum_result_ok(result):
    """Accept standard JSON true and the non-boolean success values some pools emit.

    False, None, 0 and arbitrary non-empty strings are NOT accepted.
    """
    if result is True:
        return True
    if type(result) in (int, float) and result == 1:
        return True
    if isinstance(result, str) and result.strip().lower() in ('true', '1'):
        return True
    return False


def pool_response_details(msg):
    """Explain the pool reply without logging user/password or a full network frame."""
    result = msg.get('result')
    err = msg.get('error')
    if isinstance(err, (list, tuple)):
        msg_text = str(err[1]) if len(err) > 1 and err[1] is not None else repr(err)
    elif isinstance(err, dict):
        msg_text = str(err.get('message') or err.get('reason') or repr(err))
    else:
        msg_text = repr(err)
    return f'result={result!r} ({type(result).__name__}); error={msg_text[:160]}'



def sha256d(data: bytes) -> bytes:
    return hashlib.sha256(hashlib.sha256(data).digest()).digest()


def target_from_difficulty(diff) -> int:
    try:
        with localcontext() as ctx:
            ctx.prec = 100
            d = Decimal(str(diff))
            if not d.is_finite() or d <= 0:
                raise ValueError('Pool difficulty must be positive')
            return max(1, min(MAX_TARGET, int(Decimal(DIFF1_TARGET) / d)))
    except (InvalidOperation, OverflowError) as exc:
        raise ValueError('Invalid pool difficulty') from exc


def difficulty_from_hash(digest: bytes) -> float:
    val = int.from_bytes(digest, 'little')
    return float(DIFF1_TARGET / val) if val else float('inf')


def swap_words_4(data: bytes) -> bytes:
    if len(data) != 32:
        raise ValueError('Stratum previous hash must have 32 bytes')
    return b''.join(data[k:k + 4][::-1] for k in range(0, 32, 4))


def build_header_prefix(job: dict, extranonce2: bytes) -> bytes:
    """Return the first 76 bytes of the Bitcoin 80-byte block header.

    In common Stratum V1 pools prevhash is 32-bit-word-swapped; the merkle
    branches are internal byte strings and are hashed in their supplied order.
    """
    extra1 = bytes.fromhex(job['extra1'])
    coinbase = bytes.fromhex(job['coinb1']) + extra1 + extranonce2 + bytes.fromhex(job['coinb2'])
    merkle = sha256d(coinbase)
    for branch in job['branches']:
        merkle = sha256d(merkle + bytes.fromhex(branch))
    prefix = (bytes.fromhex(job['version'])[::-1] +
              swap_words_4(bytes.fromhex(job['prevhash'])) +
              merkle + bytes.fromhex(job['ntime'])[::-1] +
              bytes.fromhex(job['nbits'])[::-1])
    if len(prefix) != 76:
        raise ValueError('Mining header prefix is not 76 bytes')
    return prefix


def parse_pool_url(text: str) -> tuple[str, int, bool]:
    url = text.strip()
    if '://' not in url:
        url = 'stratum+tcp://' + url
    parsed = urlparse(url)
    if parsed.scheme not in ('stratum+tcp', 'stratum+ssl', 'stratum+tls'):
        raise ValueError('Use stratum+tcp://host:port or stratum+ssl://host:port')
    if not parsed.hostname or not parsed.port or parsed.username or parsed.password or parsed.path not in ('', '/'):
        raise ValueError('Pool address must be host:port')
    if not (1 <= parsed.port <= 65535):
        raise ValueError('Port must be between 1 and 65535')
    return parsed.hostname, parsed.port, parsed.scheme != 'stratum+tcp'


def pool_login(cfg: dict) -> str:
    """Build the pool username without rewriting the saved wallet field.

    If a user supplied a dotted pool login (wallet.worker), preserve it verbatim.
    Otherwise use the separately configured worker name as the pool suffix.
    The same value is used for Stratum authorize and submit.
    """
    wallet = str(cfg.get('wallet', ''))
    worker = str(cfg.get('worker', ''))
    if '.' in wallet or not wallet or not worker:
        return wallet
    return f'{wallet}.{worker}'


def configure_worker_cpu(cpu_id, mining_mode):
    """Pin the hashing thread to a logical Windows CPU, without real-time priority.

    Windows may still schedule kernel tasks/interrupts on that CPU. Group-aware
    affinity supports computers with more than 64 logical CPUs when the Windows
    APIs allow that processor to be selected.
    """
    if os.name != 'nt':
        return True  # Non-Windows unit tests; the distribution is Windows-only.
    import ctypes
    k32 = ctypes.WinDLL('kernel32', use_last_error=True)
    normal = 0x00000020   # NORMAL_PRIORITY_CLASS
    below = 0x00004000   # BELOW_NORMAL_PRIORITY_CLASS
    k32.GetCurrentProcess.restype = ctypes.c_void_p
    k32.SetPriorityClass.argtypes = (ctypes.c_void_p, ctypes.c_uint32)
    k32.SetPriorityClass.restype = ctypes.c_int
    if not k32.SetPriorityClass(k32.GetCurrentProcess(),
                                normal if mining_mode == 'dedicated' else below):
        raise OSError(ctypes.get_last_error(), 'Cannot set safe worker priority')
    if cpu_id is None:
        return True  # Old scheduler test entry points only.
    if ctypes.sizeof(ctypes.c_void_p) != 8:
        raise RuntimeError('The CPU-affinity diagnostic requires 64-bit Windows.')

    class GROUP_AFFINITY(ctypes.Structure):
        _fields_ = [('Mask', ctypes.c_size_t), ('Group', ctypes.c_ushort),
                    ('Reserved', ctypes.c_ushort * 3)]

    k32.GetActiveProcessorGroupCount.restype = ctypes.c_ushort
    k32.GetActiveProcessorCount.argtypes = (ctypes.c_ushort,)
    k32.GetActiveProcessorCount.restype = ctypes.c_uint32
    group_count = k32.GetActiveProcessorGroupCount()
    remaining = cpu_id
    selected_group = selected_bit = None
    for group in range(group_count):
        size = k32.GetActiveProcessorCount(group)
        if remaining < size:
            selected_group, selected_bit = group, remaining
            break
        remaining -= size
    if selected_group is None:
        raise RuntimeError(f'Logical CPU {cpu_id} is not available on Windows')
    affinity = GROUP_AFFINITY(1 << selected_bit, selected_group, (ctypes.c_ushort * 3)())
    k32.GetCurrentThread.restype = ctypes.c_void_p
    k32.SetThreadGroupAffinity.argtypes = (ctypes.c_void_p,
                                           ctypes.POINTER(GROUP_AFFINITY), ctypes.c_void_p)
    k32.SetThreadGroupAffinity.restype = ctypes.c_int
    if not k32.SetThreadGroupAffinity(k32.GetCurrentThread(), ctypes.byref(affinity), None):
        raise OSError(ctypes.get_last_error(), f'Cannot pin miner to CPU {cpu_id}')
    return True


def worker_main(worker_id: int, count: int, work_queue, result_queue, stop_event,
                effort: int, cpu_id=None, mining_mode='background'):
    """Runs in its own process (Windows-safe with multiprocessing spawn)."""
    hashlib.sha256(b"initialization").digest()
    current = None
    extranonce_index = worker_id
    nonce = 0
    prefix = b''
    hashes = 0
    last_report = time.monotonic()
    best = 0.0
    best_value = MAX_TARGET
    try:
        configure_worker_cpu(cpu_id, mining_mode)
    except (OSError, RuntimeError) as err:
        result_queue.put(('error', worker_id, 'CPU assignment failed: ' + str(err)[:160]))
        return  # Never silently mine on the wrong CPU.
    scanner = get_scanner()
    result_queue.put(('engine', worker_id, ('Native SHA-256d, ' + scanner.engine_name) if scanner
                      else 'Python hashlib fallback'))
    native_batch = 1 << 16
    # Diagnostic-only counters. Never write files from the worker processes.
    d_since = time.monotonic()
    d_hashes = d_batches = d_job_changes = d_prefix = 0
    d_scan = d_sleep = d_idle = 0.0
    d_scan_cpu = 0.0
    d_cpu_since = time.thread_time()
    d_engine = scanner.engine_name if scanner else 'Python fallback'

    def diagnostic_flush(now):
        nonlocal d_since, d_hashes, d_batches, d_job_changes, d_prefix
        nonlocal d_scan, d_sleep, d_idle, d_engine, d_scan_cpu, d_cpu_since
        elapsed = now - d_since
        if elapsed < 2.0:
            return
        cpu_now = time.thread_time()
        sample = dict(period_s=round(elapsed, 4), hashes=d_hashes,
                      cpu_id=cpu_id if cpu_id is not None else worker_id,
                      mining_mode=mining_mode, effort_pct=effort,
                      worker_cpu_s=round(cpu_now - d_cpu_since, 5),
                      scan_cpu_s=round(d_scan_cpu, 5),
                      batches=d_batches, scan_s=round(d_scan, 5),
                      throttle_s=round(d_sleep, 5), idle_s=round(d_idle, 5),
                      job_changes=d_job_changes, prefix_builds=d_prefix,
                      batch_target=native_batch if scanner else BATCH_SIZE,
                      generation=int(current.get('generation', 0)) if current else 0,
                      engine=d_engine)
        try:
            result_queue.put_nowait(('diag', worker_id, sample))
        except queue.Full:
            pass  # telemetry never blocks mining
        d_since = now
        d_cpu_since = cpu_now
        d_hashes = d_batches = d_job_changes = d_prefix = 0
        d_scan = d_sleep = d_idle = d_scan_cpu = 0.0

    while not stop_event.is_set():
        try:
            queue_t0 = time.monotonic()
            was_idle = current is None
            if current is None:
                new_job = work_queue.get(timeout=0.12)
            else:
                new_job = work_queue.get_nowait()   # never stall a running scan
            while True:  # Drain superseded pool jobs.
                try:
                    new_job = work_queue.get_nowait()
                except queue.Empty:
                    break
            if was_idle:
                d_idle += time.monotonic() - queue_t0
            d_job_changes += 1
            current = new_job
            nonce = 0
            extranonce_index = worker_id
            prefix = b''
        except queue.Empty:
            if current is None:
                d_idle += time.monotonic() - queue_t0
                diagnostic_flush(time.monotonic())
                continue
        except (OSError, EOFError):
            return
        if current is None:
            continue
        cycle_start = time.monotonic()
        cycle_cpu_start = time.thread_time()
        if not prefix:
            try:
                ex2_size = current['extra2_size']
                ex2 = (extranonce_index % (1 << (8 * ex2_size))).to_bytes(ex2_size, 'big')
                prefix = build_header_prefix(current, ex2)
                d_prefix += 1
            except (KeyError, ValueError, TypeError) as err:
                result_queue.put(('error', worker_id, str(err)))
                current = None
                continue
        target = current['target']
        if scanner:
            amount = min(native_batch, 0x100000000 - nonce)
            try:
                scan_t0 = time.perf_counter()
                cpu_t0 = time.thread_time()
                processed, hit_nonces, candidate = scanner.scan(prefix, nonce, amount, target)
                scan_cpu = time.thread_time() - cpu_t0
                scan_dt = time.perf_counter() - scan_t0
                d_scan_cpu += scan_cpu
                d_scan += scan_dt
                d_hashes += processed
                d_batches += 1
                if processed == amount and scan_dt > 0:   # steer batch size toward ~30 ms of work
                    # Keep v0.2.1's original wall-time batch adaptation in DIAG2:
                    # CPU time is measured independently, not yet used to tune it.
                    want = int(processed * NATIVE_BATCH_SECONDS / scan_dt)
                    native_batch = max(NATIVE_BATCH_MIN, min(NATIVE_BATCH_MAX, (native_batch + want) // 2))
                hashes += processed
                value = int.from_bytes(candidate, 'little')
                if value < best_value:
                    best_value = value
                    best = (DIFF1_TARGET / value) if value else float('inf')
                    result_queue.put(('best', worker_id, best))
                for hit in hit_nonces:
                    result_queue.put(('share', worker_id, current['job_id'],
                                      ex2.hex(), current['ntime'], f'{hit:08x}',
                                      current['generation']))
                nonce = (nonce + processed) & 0xFFFFFFFF
            except Exception as err:
                result_queue.put(('error', worker_id, 'Native SHA scanner unavailable: ' + str(err)[:100]))
                scanner = None  # transparent fallback; mining does not stop
                d_engine = 'Python fallback'
        else:
            # Portable but slower Python fallback. Keep this correct for all CPUs.
            amount = min(BATCH_SIZE, 0x100000000 - nonce)
            scan_t0 = time.perf_counter()
            cpu_t0 = time.thread_time()
            for _ in range(amount):
                digest = sha256d(prefix + struct.pack('<I', nonce))
                hashes += 1
                value = int.from_bytes(digest, 'little')
                if value < best_value:
                    best_value = value
                    best = (DIFF1_TARGET / value) if value else float('inf')
                    result_queue.put(('best', worker_id, best))
                if value <= target:
                    result_queue.put(('share', worker_id, current['job_id'],
                                      ex2.hex(), current['ntime'], f'{nonce:08x}',
                                      current['generation']))
                nonce = (nonce + 1) & 0xFFFFFFFF
            d_scan += time.perf_counter() - scan_t0
            d_scan_cpu += time.thread_time() - cpu_t0
            d_hashes += amount
            d_batches += 1
        if nonce == 0:
            extranonce_index += count
            prefix = b''
        now = time.monotonic()
        if now - last_report >= 1.0:
            result_queue.put(('rate', worker_id, hashes / max(0.001, now - last_report)))
            last_report = now
            hashes = 0
        # CPU-time based effort cap: wall time already lost to Windows preemption
        # must not be counted twice. Long 1-5% sleeps remain stoppable in <=100 ms.
        if effort < 100:
            wall = time.monotonic() - cycle_start
            cpu_used = max(0.0, time.thread_time() - cycle_cpu_start)
            sleep_due = max(0.0, cpu_used * 100.0 / max(1, effort) - wall)
            sleep_t0 = time.monotonic()
            while sleep_due > 0 and not stop_event.is_set():
                begin = time.monotonic()
                stop_event.wait(min(0.10, sleep_due))
                sleep_due -= time.monotonic() - begin
            d_sleep += time.monotonic() - sleep_t0
        diagnostic_flush(time.monotonic())


class MiningEngine:
    def __init__(self, on_log=None):
        self.on_log = on_log or (lambda line: None)
        self.lock = threading.RLock()
        self.ctx = mp.get_context('spawn')
        self.running = False
        self.started_at = None
        self.config = {}
        self.connected = False
        self.authorized = False
        self.job_seen = False
        self.stage = 'Stopped'
        self.error = ''
        self.acc = 0
        self.rej = 0
        self.pending = {}
        self.best = 0.0
        self.pdiff = 0.0
        self.jobs = 0
        self.generation = 0
        self.active_job = None
        self.rates = {}
        self.work_queues = []
        self.workers = []
        self.result_queue = None
        self.stop_event = None
        self.io_stop = threading.Event()
        self.submit_queue = queue.Queue(maxsize=512)
        self.network_thread = None
        self.result_thread = None
        self.fleet_error = ''
        self.diagnostic = None
        self.diag_thread = None
        self._engine_names = {}

    def _diag_write(self, event, **values):
        diag = self.diagnostic
        if diag is not None:
            try:
                diag.write(event, **values)
            except OSError:
                pass  # diagnostics must never interrupt mining

    def _diag_monitor(self):
        while not self.io_stop.wait(2.0):
            with self.lock:
                now = time.monotonic()
                valid = [r for r, at in self.rates.values() if now - at < 4]
                stale = [now - at for _, at in self.rates.values()]
                data = dict(generation=self.generation, jobs=self.jobs,
                            workers=len(self.workers),
                            workers_alive=sum(p.is_alive() for p in self.workers),
                            reported_workers=len(valid),
                            ui_rate_mhps=round(sum(valid) / 1e6, 5) if self.running and self.job_seen else 0,
                            rate_age_max_s=round(max(stale), 3) if stale else -1,
                            mining_mode=self.config.get('mining_mode', 'background'),
                            accepted=self.acc, rejected=self.rej)
            self._diag_write('summary', **data,
                             note='stale_rate' if data['reported_workers'] < data['workers'] else '')

    def log(self, line):
        try:
            self.on_log(time.strftime('%H:%M:%S') + '  ' + str(line))
        except Exception:
            pass

    def set_stage(self, stage, error=''):
        with self.lock:
            self.stage = stage
            self.error = error
        self.log(stage + (': ' + error if error else ''))

    def start(self, config):
        if self.running:
            return
        parse_pool_url(config['pool'])
        self.config = dict(config)
        from config_and_hardware import validate_cpu_allocation
        efforts = validate_cpu_allocation(config)
        plan = [(cpu_id, amount) for cpu_id, amount in enumerate(efforts) if amount > 0]
        count = len(plan)
        mode = config.get('mining_mode', 'background')
        self.stop_event = self.ctx.Event()
        self.result_queue = self.ctx.Queue(maxsize=1024)
        self.work_queues = [self.ctx.Queue(maxsize=4) for _ in range(count)]
        self.workers = []
        self.io_stop.clear()
        self.submit_queue = queue.Queue(maxsize=512)
        with self.lock:
            self.acc = self.rej = self.jobs = 0
            self.best = self.pdiff = 0
            self.generation = 0
            self.connected = self.authorized = self.job_seen = False
            self.active_job = None
            self.rates = {}
            self.started_at = time.monotonic()
            self.running = True
        try:
            # Optional CSV telemetry only. Live dashboard and event log remain active.
            self.diagnostic = None
            if config.get('diagnostics_enabled', True):
                self.diagnostic = DiagnosticLog()
                self._diag_write('session', workers=count, mining_mode=mode, note='start')
                self.log('Diagnostic CSV: ' + str(self.diagnostic.path))
            self.log('CPU allocation: ' + mode.title() + ' | ' +
                     ', '.join(f'CPU {cpu_id}: {value}%' for cpu_id, value in plan))
            for i, (cpu_id, effort) in enumerate(plan):
                proc = self.ctx.Process(target=worker_main,
                                        args=(i, count, self.work_queues[i], self.result_queue,
                                              self.stop_event, effort, cpu_id, mode), daemon=True)
                proc.start()
                self.workers.append(proc)
            self.result_thread = threading.Thread(target=self._collect, daemon=True, name='MiningResults')
            self.network_thread = threading.Thread(target=self._network, daemon=True, name='StratumClient')
            self.result_thread.start()
            self.network_thread.start()
            if self.diagnostic is not None:
                self.diag_thread = threading.Thread(target=self._diag_monitor, daemon=True, name='DiagnosticSamples')
                self.diag_thread.start()
            self.set_stage('Connecting to pool')
        except Exception:
            self.stop()
            raise

    def stop(self):
        if not self.running:
            return
        self.io_stop.set()
        if self.stop_event:
            self.stop_event.set()
        for proc in self.workers:
            proc.join(timeout=0.4)
            if proc.is_alive():
                proc.terminate()
                proc.join(timeout=0.4)
        self.workers.clear()
        with self.lock:
            self.running = False
            self.connected = self.authorized = self.job_seen = False
            self.active_job = None
            self.rates.clear()
        # Thread waits use bounded timeouts, and workers always exit on stop.
        if self.network_thread and self.network_thread is not threading.current_thread():
            self.network_thread.join(timeout=1.5)
        if self.result_thread and self.result_thread is not threading.current_thread():
            self.result_thread.join(timeout=1.0)
        if self.diag_thread and self.diag_thread is not threading.current_thread():
            self.diag_thread.join(timeout=1.0)
        self.diag_thread = None
        self._diag_write('session', note='stop')
        if self.diagnostic:
            self.diagnostic.close()
            self.diagnostic = None
        self.set_stage('Stopped')

    def _broadcast(self, job):
        with self.lock:
            self.generation += 1
            job = dict(job, generation=self.generation)
            self.active_job = job
            self.jobs += 1
            self.job_seen = True
            # Preserve recent rate samples across job transitions. They expire
            # normally after four seconds if a worker stops reporting.
            job_count, generation = self.jobs, self.generation
        self._diag_write('new_job', jobs=job_count, generation=generation, note='new_job')
        for workq in self.work_queues:
            try:
                while True:
                    workq.get_nowait()
            except queue.Empty:
                pass
            try:
                workq.put_nowait(job)
            except queue.Full:
                self.log('Worker queue full; next job will retry')
        self.set_stage('Mining')

    def _collect(self):
        while not self.io_stop.is_set():
            try:
                event = self.result_queue.get(timeout=0.3)
            except queue.Empty:
                continue
            typ, wid, *data = event
            if typ == 'rate':
                with self.lock:
                    self.rates[wid] = (float(data[0]), time.monotonic())
            elif typ == 'best':
                with self.lock:
                    self.best = max(self.best, float(data[0]))
            elif typ == 'share':
                with self.lock:
                    if not self.connected or not self.authorized or not self.active_job or self.generation != data[4]:
                        continue
                try:
                    self.submit_queue.put_nowait((data[0], data[1], data[2], data[3]))
                except queue.Full:
                    self.log('Too many shares pending; share dropped')
            elif typ == 'engine':
                if wid == 0: self.log('SHA engine: ' + str(data[0]))
                name = str(data[0]).split(', ')[-1]
                self._engine_names[wid] = name
                self._diag_write('engine', worker_id=wid, engine=name)
            elif typ == 'diag':
                sample = data[0]
                period = max(0.001, float(sample['period_s']))
                scan = max(0.000001, float(sample['scan_s']))
                hashes_n = int(sample['hashes'])
                batches = int(sample['batches'])
                self._diag_write('worker', worker_id=wid, engine=sample['engine'],
                                 generation=sample['generation'],
                                 effort_pct=sample.get('effort_pct', 0),
                                 cpu_id=sample.get('cpu_id', wid),
                                 mining_mode=sample.get('mining_mode', 'background'),
                                 worker_cpu_s=sample.get('worker_cpu_s', 0),
                                 scan_cpu_s=sample.get('scan_cpu_s', 0),
                                 cpu_scan_mhps=round(hashes_n / max(1e-6, float(sample.get('scan_cpu_s', 0))) / 1e6, 5),
                                 cpu_busy_pct=round(100 * float(sample.get('worker_cpu_s', 0)) / period, 3),
                                 hashes=hashes_n, batches=batches,
                                 avg_batch=round(hashes_n/max(1,batches)),
                                 batch_target=sample['batch_target'],
                                 scan_s=sample['scan_s'], throttle_s=sample['throttle_s'],
                                 idle_s=sample['idle_s'], job_changes=sample['job_changes'],
                                 prefix_builds=sample['prefix_builds'],
                                 effective_mhps=round(hashes_n / period / 1e6, 5),
                                 scan_mhps=round(hashes_n / scan / 1e6, 5))
            elif typ == 'error':
                self.log(f'Worker {wid}: {data[0]}')

    def _send(self, sock, req_id, method, params):
        packet = json.dumps({'id': req_id, 'method': method, 'params': params}, separators=(',', ':')) + '\n'
        sock.sendall(packet.encode('utf-8'))

    def _network(self):
        delay = 2.0
        while not self.io_stop.is_set():
            try:
                self._session()
                delay = 2.0
            except Exception as exc:
                if not self.io_stop.is_set():
                    with self.lock:
                        self.connected = self.authorized = self.job_seen = False
                        self.rates.clear()
                        self.active_job = None
                    for q in self.work_queues:
                        try:
                            q.put_nowait(None)
                        except queue.Full:
                            pass
                    self._diag_write('reconnect', generation=self.generation,
                                     jobs=self.jobs, note='socket_restart')
                    self.set_stage('Disconnected; retrying', str(exc)[:180])
            if not self.io_stop.is_set():
                self.io_stop.wait(delay)
                delay = min(delay * 1.5, 30)

    def _session(self):
        host, port, use_tls = parse_pool_url(self.config['pool'])
        self.set_stage(f'Connecting: {host}:{port}')
        s = socket.create_connection((host, port), timeout=6)
        if use_tls:
            try:
                s = ssl.create_default_context().wrap_socket(s, server_hostname=host)
            except Exception:
                s.close()
                raise
        try:
            # Do not send buffered shares belonging to a previous pool session.
            while True:
                try:
                    self.submit_queue.get_nowait()
                except queue.Empty:
                    break
            s.settimeout(0.2)
            user = pool_login(self.config)
            # One derived pool username is used for both authorize and submit.
            # Do not log the actual wallet/account or worker name.
            self.log(f'Pool login source: Wallet/account + worker if absent; {len(user)} username characters; '
                     f'password configured: {bool(self.config.get("password", ""))}')
            seq = 10
            self._send(s, 1, 'mining.subscribe', [ESP_STRATUM_AGENT])
            subscribed = authorized = False
            pending_diff = 1.0
            extra1 = ''
            extra2_size = 4
            last_job = None
            buffer = b''
            with self.lock:
                self.connected = True
                self.authorized = self.job_seen = False
                self.active_job = None
                self.rates.clear()
            self.set_stage('Subscribing')
            while not self.io_stop.is_set():
                # Submit shares only on the connected pool socket.
                if subscribed and authorized:
                    for _ in range(1):
                        try:
                            job_id, extra2, ntime, nonce = self.submit_queue.get_nowait()
                        except queue.Empty:
                            break
                        seq += 1
                        self._send(s, seq, 'mining.submit', [user, job_id, extra2, ntime, nonce])
                        with self.lock:
                            self.pending[seq] = time.monotonic()
                try:
                    data = s.recv(65536)
                    if not data:
                        raise ConnectionError('Pool closed the connection')
                    buffer += data
                    if len(buffer) > MAX_LINE_SIZE:
                        raise ValueError('Pool sent an oversized message')
                except socket.timeout:
                    continue
                while b'\n' in buffer:
                    raw, buffer = buffer.split(b'\n', 1)
                    if not raw.strip():
                        continue
                    try:
                        msg = json.loads(raw.decode('utf-8'))
                    except (ValueError, UnicodeDecodeError):
                        self.log('Ignored malformed Stratum JSON')
                        continue
                    if not isinstance(msg, dict):
                        continue
                    method = msg.get('method')
                    params = msg.get('params') or []
                    if method == 'mining.set_difficulty' and params:
                        pending_diff = float(params[0])
                        target_from_difficulty(pending_diff)
                        self.log(f'Next job difficulty: {pending_diff:g}')
                    elif method == 'mining.set_extranonce' and len(params) >= 2:
                        extra1, extra2_size = str(params[0]), int(params[1])
                        if not 1 <= extra2_size <= 16:
                            raise ValueError('Invalid extranonce2 size')
                        last_job = None
                        with self.lock:
                            self.active_job = None
                    elif method == 'mining.notify' and len(params) >= 9:
                        if not subscribed:
                            continue
                        # Difficulty applies when a new job is issued, not retroactively
                        # to a previously issued job (standard Stratum V1 behavior).
                        try:
                            last_job = {
                                'job_id': str(params[0]), 'prevhash': str(params[1]),
                                'coinb1': str(params[2]), 'coinb2': str(params[3]),
                                'branches': list(params[4]), 'version': str(params[5]),
                                'nbits': str(params[6]), 'ntime': str(params[7]),
                                'clean': bool(params[8]), 'extra1': extra1,
                                'extra2_size': extra2_size, 'target': target_from_difficulty(pending_diff),
                            }
                            build_header_prefix(last_job, bytes(extra2_size))
                        except (ValueError, TypeError, KeyError) as exc:
                            self.log(f'Bad pool job ignored: {exc}')
                            last_job = None
                        if last_job and authorized:
                            with self.lock:
                                self.pdiff = pending_diff
                            self._broadcast(last_job)
                    elif method == 'client.show_message' and params:
                        self.log('Pool: ' + str(params[0])[:200])
                    elif method == 'client.get_version' and msg.get('id') is not None:
                        s.sendall((json.dumps({'id': msg['id'],
                            'result': ESP_STRATUM_AGENT, 'error': None}) + '\n').encode('utf-8'))
                    elif msg.get('id') == 1:
                        result = msg.get('result')
                        if not result or len(result) < 3:
                            raise ConnectionError('Subscription refused: ' + str(msg.get('error'))[:160])
                        extra1, extra2_size = str(result[1]), int(result[2])
                        if not 1 <= extra2_size <= 16:
                            raise ValueError('Invalid extranonce2 size')
                        subscribed = True
                        # Match the working ESP32 order: subscribe -> authorize.
                        # Do not send the optional difficulty suggestion before login.
                        self._send(s, 2, 'mining.authorize', [user, self.config.get('password', '')])
                        self.set_stage('Authorizing')
                    elif msg.get('id') == 2:
                        if not stratum_result_ok(msg.get('result')):
                            raise ConnectionError('Pool refused authorization: ' + pool_response_details(msg))
                        if msg.get('error') not in (None, False):
                            raise ConnectionError('Conflicting authorization response: ' + pool_response_details(msg))
                        authorized = True
                        with self.lock:
                            self.authorized = True
                        self.set_stage('Authorized; waiting for work')
                        # The ESP32 sends suggest_difficulty AFTER authorize request;
                        # sending it after authorization confirmation also avoids pools
                        # that reject requests while the worker is not yet logged in.
                        if str(self.config.get('suggested_difficulty', '')).strip():
                            self._send(s, 3, 'mining.suggest_difficulty', [float(self.config['suggested_difficulty'])])
                        if last_job:
                            with self.lock:
                                self.pdiff = pending_diff
                            self._broadcast(last_job)
                    elif isinstance(msg.get('id'), int) and msg['id'] >= 11:
                        with self.lock:
                            submitted = self.pending.pop(msg['id'], None)
                            if submitted is not None:
                                if stratum_result_ok(msg.get('result')):
                                    self.acc += 1
                                    self.log(f'Share accepted (total {self.acc})')
                                else:
                                    self.rej += 1
                                    self.log('Share rejected: ' + str(msg.get('error'))[:160])
        finally:
            with self.lock:
                self.pending.clear()
                self.connected = self.authorized = self.job_seen = False
                self.active_job = None
                self.rates.clear()
            s.close()

    def snapshot(self):
        with self.lock:
            now = time.monotonic()
            rate = sum(r for r, at in self.rates.values() if now - at < 4) if self.running and self.job_seen else 0
            name = self.config.get('name', 'pc01') or 'pc01'
            pool = self.config.get('pool', '')
            result = dict(host=f'crsminer-{name.lower()}.local', name=name.lower(),
                          hr=round(rate, 2), acc=self.acc, rej=self.rej,
                          ok=bool(self.running and self.authorized and self.job_seen and self.connected),
                          stage=self.stage, err=self.error, diag=self.error,
                          pool=pool, best=round(self.best, 9), pdiff=self.pdiff,
                          up=int(now - self.started_at) if self.running and self.started_at else 0,
                          jobs=self.jobs, rssi=0, temp=0, mac='',
                          mode='CPU SHA256d', platform='windows', http_port=8080,
                          threads=len(self.workers), effort=int(self.config.get('effort', 0)),
                          mining_mode=self.config.get('mining_mode', 'background'),
                          ip='', running=self.running, worker=self.config.get('worker', ''))
            return result
