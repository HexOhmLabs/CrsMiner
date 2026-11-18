"""Validate the native nonce scanner on platforms where it can be loaded.

On Windows x64 this tests the bundled DLL. On other systems it is skipped
unless CRS_NATIVE_LIB_TEST is set to a test-built shared object.
"""
import ctypes
import hashlib
import random
import struct
import unittest
from pathlib import Path
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from native_hash import get_scanner
from miner_core import target_from_difficulty

class NativeScanTests(unittest.TestCase):
    def setUp(self):
        self.scanner=get_scanner()
        if self.scanner is None:
            self.skipTest('No native Windows x64 DLL available on this platform')

    def test_100_exact_nonce_hashes(self):
        rand=random.Random(2272)
        lib=self.scanner.lib
        for i in range(100):
            prefix=rand.randbytes(76)
            nonce=rand.randrange(0,2**32)
            ib=(ctypes.c_ubyte*76).from_buffer_copy(prefix)
            ob=(ctypes.c_ubyte*32)()
            lib.crs_hash_nonce(ib,nonce,ob)
            expect=hashlib.sha256(hashlib.sha256(prefix+struct.pack('<I',nonce)).digest()).digest()
            self.assertEqual(bytes(ob),expect)

    def test_matches_and_best_hash(self):
        rand=random.Random(814)
        prefix=rand.randbytes(76)
        first=100
        # High target generates numerous hits; scanner has bounded output and
        # must resume without losing a nonce.
        target=target_from_difficulty(0.00000001)
        total=0
        observed=[]
        best=None
        while total<800:
            count,hits,candidate=self.scanner.scan(prefix,first+total,min(200,800-total),target)
            self.assertGreater(count,0)
            total+=count
            observed.extend(hits)
            if best is None or int.from_bytes(candidate,'little')<int.from_bytes(best,'little'):
                best=candidate
        digests=[(n,hashlib.sha256(hashlib.sha256(prefix+struct.pack('<I',n)).digest()).digest()) for n in range(first,first+800)]
        self.assertEqual(observed,[n for n,h in digests if int.from_bytes(h,'little')<=target])
        self.assertEqual(best,min((h for _,h in digests),key=lambda v:int.from_bytes(v,'little')))


class EngineTests(unittest.TestCase):
    """Every engine the CPU supports must give bit-identical results."""
    def setUp(self):
        self.scanner = get_scanner()
        if self.scanner is None:
            self.skipTest('No native library available on this platform')
        self.engines = [e for e in ('scalar', 'shani', 'avx2') if self.scanner.set_engine(e) >= 0]
        self.assertIn('scalar', self.engines)

    def tearDown(self):
        if self.scanner is not None:
            self.scanner.lib.crs_set_engine(-1)   # back to auto

    @staticmethod
    def ref(prefix, start, count, target):
        hits, best = [], None
        for n in range(start, start + count):
            d = hashlib.sha256(hashlib.sha256(prefix + struct.pack('<I', n & 0xFFFFFFFF)).digest()).digest()
            v = int.from_bytes(d, 'little')
            if best is None or v < best: best = v
            if v <= target: hits.append(n & 0xFFFFFFFF)
        return hits, best

    def test_all_engines_match_hashlib(self):
        rand = random.Random(99)
        for eng in self.engines:
            self.scanner.set_engine(eng)
            for _ in range(25):
                prefix = rand.randbytes(76)
                start = rand.choice([0, 1, 7, 0xFFFFFFF0, rand.getrandbits(32)])
                count = min(rand.choice([1, 2, 3, 7, 8, 9, 15, 16, 17, 130]), 2**32 - start)
                target = rand.choice([2**256 - 1, 2**252, 2**249, 2**100, 0])
                done, hits, best = self.scanner.scan(prefix, start, count, target)
                exp_hits, exp_best = self.ref(prefix, start, done, target)
                self.assertEqual(hits, exp_hits, f'{eng} hits')
                self.assertEqual(int.from_bytes(best, 'little'), exp_best, f'{eng} best')
                if len(hits) < 256:
                    self.assertEqual(done, count)

    def test_full_hit_buffer_resumes_without_loss(self):
        prefix = random.Random(5).randbytes(76)
        for eng in self.engines:
            self.scanner.set_engine(eng)
            seen, pos = [], 0
            while pos < 1000:
                done, hits, _ = self.scanner.scan(prefix, pos, 1000 - pos, 2**256 - 1)
                self.assertGreater(done, 0)
                seen += hits; pos += done
            self.assertEqual(seen, list(range(1000)), eng)

    def test_zero_count_and_engine_report(self):
        done, hits, best = self.scanner.scan(bytes(76), 0, 0, 2**200)
        self.assertEqual((done, hits, best), (0, [], b'\xff' * 32))
        self.assertIn(self.scanner.engine_name, ('scalar C', 'SHA-NI', 'AVX2 x8'))
