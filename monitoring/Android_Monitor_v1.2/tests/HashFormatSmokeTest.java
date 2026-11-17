package com.hexohm.crsmining;

final class HashFormatSmokeTest {
    public static void main(String[] args) {
        assertRate(0, "0.00", "H/s");
        assertRate(12500, "12.5", "KH/s");
        assertRate(1234567, "1.23", "MH/s");
        assertRate(-5, "—", "H/s");
        assertRate(Double.NaN, "—", "H/s");
        System.out.println("HashFormat smoke tests passed");
    }
    private static void assertRate(double value, String number, String units) {
        String[] parts = HashFormat.parts(value);
        if (!number.equals(parts[0]) || !units.equals(parts[1])) {
            throw new AssertionError("For " + value + ": " + parts[0] + " " + parts[1]);
        }
    }
}
