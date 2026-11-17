package com.hexohm.crsmining;

import java.util.ArrayDeque;
import java.util.concurrent.atomic.AtomicBoolean;

/** Live data returned by CRSminer-8's read-only JSON endpoints. */
final class MinerData {
    final String ip;
    volatile String hostname;
    volatile String worker = "";
    volatile String pool = "";
    volatile String wallet = "";
    volatile String mac = "";
    volatile String stage = "";
    volatile String error = "";
    volatile String diagnostic = "";
    volatile String shaMode = "";
    volatile double hashesPerSecond;
    volatile double bestDifficulty;
    volatile double poolDifficulty;
    volatile double chipTemperature;
    volatile long accepted;
    volatile long rejected;
    volatile long uptimeSeconds;
    volatile long jobs;
    volatile int rssi;
    volatile boolean poolReady;
    volatile boolean reachable = true;
    volatile long lastConfigReadMs;
    volatile int missedPolls = 0;
    final AtomicBoolean polling = new AtomicBoolean(false);
    final ArrayDeque<Double> history = new ArrayDeque<>();

    MinerData(String ip, String hostname) {
        this.ip = ip;
        this.hostname = hostname;
    }

    void updateTelemetry(MinerData other) {
        hostname = other.hostname;
        hashesPerSecond = other.hashesPerSecond;
        accepted = other.accepted;
        rejected = other.rejected;
        poolReady = other.poolReady;
        bestDifficulty = other.bestDifficulty;
        poolDifficulty = other.poolDifficulty;
        chipTemperature = other.chipTemperature;
        uptimeSeconds = other.uptimeSeconds;
        jobs = other.jobs;
        rssi = other.rssi;
        mac = other.mac;
        stage = other.stage;
        error = other.error;
        diagnostic = other.diagnostic;
        shaMode = other.shaMode;
        if (pool.isEmpty()) pool = other.pool;
        missedPolls = 0;
        reachable = true;
        history.addLast(Math.max(0, hashesPerSecond));
        while (history.size() > 40) history.removeFirst();
    }
}
