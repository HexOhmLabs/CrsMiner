"""HexOhm CRSminer PC - offline speed test (no pool, no network).

    python bench.py            # every engine your CPU supports, one core, then the full worker pipeline
    python bench.py --seconds 3 --workers 4

Prints hashes/second per engine and for real worker processes (the same code path the miner uses).
"""
import argparse
import hashlib
import multiprocessing as mp
import os
import queue
import struct
import time

from native_hash import get_scanner
import miner_core

JOB = dict(job_id='bench', extra1='00000000', extra2_size=4, coinb1='01' * 40, coinb2='02' * 20,
           branches=[], version='20000000', prevhash='00' * 32, ntime='65000000', nbits='1d00ffff',
           target=miner_core.target_from_difficulty(1_000_000), generation=1)


def fmt(h):
    for unit, div in (('GH/s', 1e9), ('MH/s', 1e6), ('kH/s', 1e3)):
        if h >= div:
            return f'{h / div:8.2f} {unit}'
    return f'{h:8.1f}  H/s'


def one_core(seconds):
    sc = get_scanner()
    if sc is None:
        print('Native engine not available -> pure-Python hashlib fallback only.')
        t = time.perf_counter(); n = 0
        while time.perf_counter() - t < seconds:
            hashlib.sha256(hashlib.sha256(bytes(80) + struct.pack('<I', n)).digest()).digest(); n += 1
        print(f'  python fallback  {fmt(n / (time.perf_counter() - t))}')
        return
    prefix = os.urandom(76); target = JOB['target']
    print(f'CPU features bitmask: {sc.lib.crs_cpu_features() if hasattr(sc.lib, "crs_cpu_features") else "n/a"}')
    for name in ('scalar', 'avx2', 'shani'):
        if sc.set_engine(name) < 0:
            print(f'  {name:7s} not supported by this CPU'); continue
        total, t = 0, time.perf_counter()
        while time.perf_counter() - t < seconds:
            done, _, _ = sc.scan(prefix, total & 0xFFFFFFFF, 1 << 21, target); total += done
        print(f'  {name:7s} {fmt(total / (time.perf_counter() - t))}  (one core, {sc.engine_name})')
    sc.lib.crs_set_engine(-1)
    print(f'  auto-selected engine: {sc.engine_name}')


def pipeline(workers, seconds):
    ctx = mp.get_context('spawn')
    stop, results = ctx.Event(), ctx.Queue()
    queues = [ctx.Queue(maxsize=4) for _ in range(workers)]
    procs = [ctx.Process(target=miner_core.worker_main, args=(i, workers, queues[i], results, stop, 100), daemon=True)
             for i in range(workers)]
    for p in procs: p.start()
    for q in queues: q.put(JOB)
    rates, engine, deadline = {}, '', time.time() + seconds + 3
    while time.time() < deadline:
        try:
            ev = results.get(timeout=0.5)
        except queue.Empty:
            continue
        if ev[0] == 'rate': rates[ev[1]] = ev[2]
        elif ev[0] == 'engine' and not engine: engine = ev[2]
    stop.set()
    for p in procs: p.join(timeout=2)
    print(f'  {workers} worker process(es) [{engine}]: total {fmt(sum(rates.values()))}'
          + (f'   (per process {fmt(sum(rates.values()) / max(1, len(rates)))})' if rates else ''))


if __name__ == '__main__':
    mp.freeze_support()
    ap = argparse.ArgumentParser()
    ap.add_argument('--seconds', type=float, default=2.0)
    ap.add_argument('--workers', type=int, default=min(2, os.cpu_count() or 1))
    a = ap.parse_args()
    print('--- single core, native engines ---'); one_core(a.seconds)
    print('--- full worker pipeline ---'); pipeline(a.workers, a.seconds)
