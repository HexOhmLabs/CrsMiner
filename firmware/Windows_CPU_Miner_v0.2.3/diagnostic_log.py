"""Diagnostic-only, privacy-restricted throughput CSV. Not part of stable v0.2.1."""
from __future__ import annotations
import csv
from datetime import datetime
from pathlib import Path
import re
import threading
import time
from config_and_hardware import user_data_dir

# Only approved numeric telemetry and fixed event names are writable.
# Pool host, wallet/account, password, worker labels, machine name and raw
# Stratum frames are never included in this schema.
FIELDS = (
    'time_local','elapsed_s','event','worker_id','engine','generation',
    'jobs','effort_pct','cpu_id','mining_mode','workers','workers_alive','reported_workers',
    'ui_rate_mhps','effective_mhps','scan_mhps','hashes','batches',
    'avg_batch','batch_target','scan_s','scan_cpu_s','worker_cpu_s',
    'cpu_scan_mhps','cpu_busy_pct','throttle_s','idle_s',
    'job_changes','prefix_builds','rate_age_max_s','accepted','rejected',
    'note'
)
EVENTS = {'session','summary','worker','engine','new_job','reconnect'}
ENGINES = {'SHA-NI', 'AVX2 x8', 'scalar C', 'Python fallback', 'Unknown'}
NOTES = {'start', 'stop', 'new_job', 'socket_restart', 'stale_rate', ''}

# Match only CSVs created by this miner; never delete unrelated files.
DIAGNOSTIC_FILENAME = re.compile(r'^CRSminer_PC_DIAG_\d{8}_\d{6}_\d{6}\.csv$', re.ASCII)


def diagnostics_directory(create=False):
    directory = user_data_dir() / 'diagnostics'
    if create:
        directory.mkdir(parents=True, exist_ok=True)
    return directory


def diagnostic_csv_files():
    directory = diagnostics_directory()
    if directory.is_symlink():
        raise OSError('Refusing diagnostics cleanup through a symbolic-link directory.')
    if not directory.is_dir():
        return []
    return sorted(path for path in directory.iterdir()
                  if DIAGNOSTIC_FILENAME.fullmatch(path.name) and path.is_file()
                  and not path.is_symlink())


def clear_diagnostic_csv_files():
    """Delete only CRSminer-generated CSVs. Call only while mining is stopped."""
    deleted = 0
    failures = []
    for path in diagnostic_csv_files():
        try:
            path.unlink()
            deleted += 1
        except OSError as exc:
            failures.append((path.name, str(exc)))
    return deleted, failures


class DiagnosticLog:
    def __init__(self):
        directory = diagnostics_directory(create=True)
        # Timestamp includes microseconds to avoid overwriting sessions.
        name = 'CRSminer_PC_DIAG_' + datetime.now().strftime('%Y%m%d_%H%M%S_%f') + '.csv'
        self.path = directory / name
        self._file = self.path.open('w', newline='', encoding='utf-8-sig')
        self._writer = csv.DictWriter(self._file, fieldnames=FIELDS, extrasaction='ignore')
        self._writer.writeheader()
        self._lock = threading.Lock()
        self._t0 = time.monotonic()

    def write(self, event: str, **values):
        if event not in EVENTS:
            return
        entry = {k: values.get(k, '') for k in FIELDS}
        # Free-text input can contain credentials; allow only fixed labels.
        entry['event'] = event
        entry['engine'] = entry['engine'] if entry['engine'] in ENGINES else 'Unknown'
        entry['mining_mode'] = (entry['mining_mode'] if entry['mining_mode'] in
                                ('background', 'dedicated') else '')
        entry['note'] = entry['note'] if entry['note'] in NOTES else ''
        entry['time_local'] = datetime.now().astimezone().isoformat(timespec='milliseconds')
        entry['elapsed_s'] = round(time.monotonic() - self._t0, 3)
        with self._lock:
            if not self._file.closed:
                self._writer.writerow(entry)
                self._file.flush()

    def close(self):
        with self._lock:
            if not self._file.closed:
                self._file.close()
