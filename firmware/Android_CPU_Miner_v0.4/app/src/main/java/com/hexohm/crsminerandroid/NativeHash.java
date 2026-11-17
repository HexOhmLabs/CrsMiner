package com.hexohm.crsminerandroid;

final class NativeHash {
    static { System.loadLibrary("crs_sha256d"); }
    /** Returns nonce of a pool-valid hash or ESP-style 12-leading-zero near-miss; -1 if none.
     * Caller independently verifies all candidates, submits only pool-valid hashes. */
    static native long scan(byte[] header76, byte[] targetBigEndian, long startNonce, int maxCount);
    static native boolean selfTest();
}
