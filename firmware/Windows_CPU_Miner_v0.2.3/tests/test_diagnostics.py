"""Diagnostic instrumentation tests: separate from stable v0.2.1."""
import csv
import multiprocessing as mp
import queue
import tempfile
import time
import unittest
from pathlib import Path
from unittest.mock import patch

from diagnostic_log import DiagnosticLog, FIELDS
from miner_core import worker_main


class DiagnosticTests(unittest.TestCase):
    def test_diagnostic_csv_only_uses_allowed_fields_and_fixed_labels(self):
        with tempfile.TemporaryDirectory() as folder:
            with patch('diagnostic_log.user_data_dir', return_value=Path(folder)):
                recorder = DiagnosticLog()
                path = recorder.path
                recorder.write('session', note='start', workers=2, wallet='secretwallet', password='secretpassword')
                recorder.write('worker', worker_id=0, engine='SHA-NI', mining_mode='dedicated', cpu_id=0, hashes=400000,
                               note='secretpassword', wallet='secretwallet')
                recorder.write('an_invalid_event', note='secretwallet')
                recorder.close()
            text = path.read_text(encoding='utf-8-sig')
            self.assertNotIn('secretwallet', text)
            self.assertNotIn('secretpassword', text)
            with path.open(newline='', encoding='utf-8-sig') as f:
                rows = list(csv.DictReader(f))
            self.assertEqual(len(rows), 2)
            self.assertEqual(rows[0]['event'], 'session')
            self.assertEqual(rows[0]['note'], 'start')
            self.assertEqual(rows[1]['engine'], 'SHA-NI')
            self.assertEqual(rows[1]['note'], '')
            self.assertEqual(rows[1]['mining_mode'], 'dedicated')
            self.assertEqual(set(rows[0]), set(FIELDS))

    def test_worker_emits_real_scan_and_throttle_timing(self):
        ctx = mp.get_context('spawn')
        jq, rq, stop = ctx.Queue(maxsize=4), ctx.Queue(maxsize=50), ctx.Event()
        job = {'job_id':'unit-job', 'prevhash':'00'*32,
               'coinb1':'010203', 'coinb2':'040506', 'extra1':'aabb',
               'extra2_size':2, 'branches':[], 'version':'20000000',
               'nbits':'1d00ffff', 'ntime':'495fab29', 'target':0,
               'generation':1}
        proc = ctx.Process(target=worker_main, args=(0,1,jq,rq,stop,50))
        proc.start()
        try:
            jq.put(job, timeout=2)
            sample = None
            deadline = time.monotonic()+9
            while time.monotonic()<deadline:
                try:
                    event = rq.get(timeout=.5)
                except queue.Empty:
                    continue
                if event[0]=='diag' and event[2]['hashes']>0:
                    sample=event[2]
                    break
            self.assertIsNotNone(sample, 'No worker diagnostics arrived')
            self.assertGreater(sample['scan_s'],0)
            self.assertGreater(sample['worker_cpu_s'], 0)
            self.assertGreater(sample['scan_cpu_s'], 0)
            self.assertEqual(sample['mining_mode'], 'background')
            self.assertGreater(sample['throttle_s'],0)
            self.assertGreater(sample['batches'],0)
            self.assertGreater(sample['hashes'],0)
            self.assertEqual(sample['generation'],1)
        finally:
            stop.set()
            proc.join(timeout=3)
            if proc.is_alive():
                proc.terminate()
                proc.join(timeout=3)
            jq.close(); rq.close()

if __name__=='__main__':
    unittest.main()
