"""Verify active workers never wait for a timed queue read; ~30 ms adaptive batches."""
import queue
import sys
import unittest
from pathlib import Path
from unittest.mock import patch
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import miner_core as core


class SchedulerTests(unittest.TestCase):
    def test_native_batch_limits_and_30ms_budget(self):
        self.assertAlmostEqual(core.NATIVE_BATCH_SECONDS, 0.030)
        self.assertGreaterEqual(core.NATIVE_BATCH_MIN, 1)
        self.assertGreater(core.NATIVE_BATCH_MAX, core.NATIVE_BATCH_MIN)

    def test_active_worker_uses_nonblocking_job_check(self):
        class WorkQueue:
            blocking_calls = 0
            nonblocking_calls = 0
            def get(self, timeout):
                self.blocking_calls += 1
                if self.blocking_calls != 1:
                    raise AssertionError('Active worker blocked waiting for another job')
                return {'extra2_size':4, 'target':0, 'job_id':'mock',
                        'ntime':'60000000', 'generation':1}
            def get_nowait(self):
                self.nonblocking_calls += 1
                raise queue.Empty
        class Stop:
            done=False
            def is_set(self): return self.done
        class Result:
            def __init__(self): self.events=[]
            def put(self, e): self.events.append(e)
        class Scanner:
            engine_name='test-native'
            def __init__(self, stop): self.calls=0; self.stop=stop
            def scan(self, prefix, nonce, amount, target):
                self.calls+=1
                if self.calls>=3: self.stop.done=True
                return amount, [], b'\xff'*32
        q, result, stop=WorkQueue(), Result(), Stop()
        sc=Scanner(stop)
        with patch.object(core,'get_scanner',return_value=sc), \
             patch.object(core,'build_header_prefix',return_value=bytes(76)):
            core.worker_main(0,1,q,result,stop,100)
        self.assertEqual(q.blocking_calls,1)
        self.assertGreaterEqual(q.nonblocking_calls,3)
        self.assertEqual(sc.calls,3)
        self.assertTrue(any(e[0]=='engine' for e in result.events))

    def test_pool_username_suffixed_only_when_needed(self):
        self.assertEqual(core.pool_login({'wallet':'DGBwallet','worker':'pc01'}),'DGBwallet.pc01')
        self.assertEqual(core.pool_login({'wallet':'DGBwallet.worker','worker':'pc01'}),'DGBwallet.worker')
        self.assertEqual(core.pool_login({'wallet':'DGBwallet','worker':''}),'DGBwallet')

if __name__=='__main__': unittest.main()
