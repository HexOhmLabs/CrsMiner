"""Native CPU SHA-256d nonce scanning (bundled freestanding DLL on Windows x64).

The DLL picks the fastest engine the CPU supports (SHA-NI, AVX2 8-way, or scalar).
No compiler or runtime download is needed on the target machine. If the DLL cannot
load, get_scanner() returns None and the slower pure-Python path is used.

Environment overrides (diagnostics only):
  CRS_NATIVE_LIB_TEST  path to a separately built shared library (used by the tests)
  CRS_ENGINE           force 'scalar', 'shani' or 'avx2' (ignored if the CPU lacks it)
"""
from __future__ import annotations
import ctypes
import os
import struct
import sys
from pathlib import Path

_BATCH_CAP = 256
ENGINE_NAMES = {0: 'scalar C', 1: 'SHA-NI', 2: 'AVX2 x8'}
_ENGINE_IDS = {'scalar': 0, 'shani': 1, 'sha-ni': 1, 'avx2': 2}


def get_scanner():
    path = None
    if os.environ.get('CRS_NATIVE_LIB_TEST'):
        path = Path(os.environ['CRS_NATIVE_LIB_TEST'])
    elif sys.platform == 'win32' and struct.calcsize('P') == 8:
        root = Path(getattr(sys, '_MEIPASS', Path(__file__).resolve().parent))
        path = root / 'native' / 'crs_sha256d_win64.dll'
    if path is None:
        return None
    try:
        lib = ctypes.CDLL(str(path))
        ptr = ctypes.POINTER(ctypes.c_ubyte)
        lib.crs_scan.argtypes = [ptr, ctypes.c_uint32, ctypes.c_uint32, ptr,
                                 ctypes.POINTER(ctypes.c_uint32), ctypes.c_uint32,
                                 ctypes.POINTER(ctypes.c_uint32), ptr]
        lib.crs_scan.restype = ctypes.c_uint32
        lib.crs_hash_nonce.argtypes = [ptr, ctypes.c_uint32, ptr]
        lib.crs_hash_nonce.restype = None
        lib.crs_engine.argtypes = []
        lib.crs_engine.restype = ctypes.c_int
        lib.crs_set_engine.argtypes = [ctypes.c_int]
        lib.crs_set_engine.restype = ctypes.c_int
        forced = os.environ.get('CRS_ENGINE', '').strip().lower()
        if forced in _ENGINE_IDS:
            lib.crs_set_engine(_ENGINE_IDS[forced])   # -1 (unsupported) leaves auto-detection in place
        return NativeScanner(lib)
    except (OSError, AttributeError, TypeError):
        return None


class NativeScanner:
    def __init__(self, lib):
        self.lib = lib
        self.prefix = None
        self.prefix_buf = (ctypes.c_ubyte * 76)()
        self.target = None
        self.target_buf = (ctypes.c_ubyte * 32)()
        self.hits = (ctypes.c_uint32 * _BATCH_CAP)()
        self.best = (ctypes.c_ubyte * 32)()
        self.hit_count = ctypes.c_uint32(0)
        self.hit_ref = ctypes.byref(self.hit_count)

    @property
    def engine_name(self) -> str:
        return ENGINE_NAMES.get(self.lib.crs_engine(), 'native')

    def set_engine(self, name: str) -> int:
        return self.lib.crs_set_engine(_ENGINE_IDS[name])

    def set_prefix(self, prefix: bytes):
        if len(prefix) != 76:
            raise ValueError('Expected 76-byte header prefix')
        if self.prefix != prefix:
            self.prefix = prefix
            ctypes.memmove(self.prefix_buf, prefix, 76)

    def scan(self, prefix: bytes, nonce: int, count: int, target: int):
        self.set_prefix(prefix)
        if self.target != target:
            self.target = target
            ctypes.memmove(self.target_buf, target.to_bytes(32, 'little'), 32)
        processed = self.lib.crs_scan(self.prefix_buf, nonce, count, self.target_buf,
                                      self.hits, _BATCH_CAP, self.hit_ref, self.best)
        if processed == 0 and count:
            raise RuntimeError('Native SHA scanner processed no hashes')
        return processed, self.hits[:self.hit_count.value], bytes(self.best)
