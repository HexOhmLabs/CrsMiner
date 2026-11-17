package com.hexohm.crsmining;

import java.util.Locale;

/** Pure Java helper: independent of Android for straightforward smoke tests. */
final class HashFormat {
    private HashFormat() { }

    static String[] parts(double hashesPerSecond) {
        if (!Double.isFinite(hashesPerSecond) || hashesPerSecond < 0) return new String[] {"—", "H/s"};
        String[] units = {"H/s", "KH/s", "MH/s", "GH/s", "TH/s", "PH/s"};
        int unit = 0;
        while (hashesPerSecond >= 1000.0 && unit < units.length - 1) {
            hashesPerSecond /= 1000.0;
            unit++;
        }
        int precision = hashesPerSecond < 10 ? 2 : hashesPerSecond < 100 ? 1 : 0;
        String format = "%1$." + precision + "f";
        return new String[] {String.format(Locale.US, format, hashesPerSecond), units[unit]};
    }
}
