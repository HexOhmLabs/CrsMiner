package com.hexohm.crsmining;

import android.net.Network;

import org.json.JSONException;
import org.json.JSONObject;

import java.io.BufferedReader;
import java.io.IOException;
import java.io.InputStream;
import java.io.InputStreamReader;
import java.net.HttpURLConnection;
import java.net.InetSocketAddress;
import java.net.Socket;
import java.net.URL;
import java.nio.charset.StandardCharsets;
import java.util.Locale;

/** Read-only access to ESP CRSminer on :80 and Android CRSminer on :8080. */
final class MinerClient {
    private static final int TCP_TIMEOUT_MS = 380;
    private static final int HTTP_CONNECT_TIMEOUT_MS = 1100;
    private static final int HTTP_READ_TIMEOUT_MS = 1600;

    private MinerClient() { }

    static MinerData discover(Network network, String ip) {
        // 80 = ESP32 CRSminer; 8080 = CRSminer for Android.
        // Keep ESP's existing IP-only identifier unchanged to preserve saved layout/details.
        for (int port : new int[]{80, 8080}) {
            // Important: close the probe socket BEFORE opening a second HTTP connection.
            // ESP WebServer and the Android miner's HTTP loop handle clients serially;
            // keeping this socket open while requesting /api/status can stall both.
            try (Socket socket = network.getSocketFactory().createSocket()) {
                socket.connect(new InetSocketAddress(ip, port), TCP_TIMEOUT_MS);
            } catch (IOException | SecurityException ignored) {
                continue;
            }
            String endpoint = port == 80 ? ip : ip + ":8080";
            MinerData m = status(network, endpoint);
            if (m != null) return m;
        }
        return null;
    }

    static MinerData status(Network network, String ip) {
        try {
            JSONObject j = getJson(network, ip, "/api/status");
            if (j == null || !isCrsminer(j)) return null;
            MinerData m = new MinerData(ip, j.getString("host"));
            m.hashesPerSecond = j.optDouble("hr", 0);
            m.accepted = j.optLong("acc", 0);
            m.rejected = j.optLong("rej", 0);
            m.poolReady = j.optBoolean("ok", false);
            m.pool = j.optString("pool", "");
            m.bestDifficulty = j.optDouble("best", 0);
            m.poolDifficulty = j.optDouble("pdiff", 0);
            m.uptimeSeconds = j.optLong("up", 0);
            m.jobs = j.optLong("jobs", 0);
            m.rssi = j.optInt("rssi", 0);
            m.chipTemperature = j.optDouble("temp", 0);
            m.mac = j.optString("mac", "");
            m.stage = j.optString("stage", "");
            m.error = j.optString("err", "");
            m.diagnostic = j.optString("diag", "");
            m.shaMode = j.optString("mode", "");
            return m;
        } catch (JSONException e) {
            return null;
        }
    }

    static Config readConfig(Network network, String ip) {
        JSONObject j = getJson(network, ip, "/api/config");
        if (j == null || !j.has("worker") || !j.has("addr")) return null;
        Config cfg = new Config();
        cfg.worker = j.optString("worker", "");
        cfg.pool = j.optString("pool", "");
        cfg.wallet = j.optString("addr", "");
        return cfg;
    }

    static final class Config {
        String worker = "";
        String pool = "";
        String wallet = "";
    }

    private static boolean isCrsminer(JSONObject j) {
        String host = j.optString("host", "").toLowerCase(Locale.ROOT);
        return host.startsWith("crsminer-") && host.endsWith(".local")
                && j.has("hr") && j.has("acc") && j.has("rej") && j.has("ok");
    }

    private static JSONObject getJson(Network network, String ip, String path) {
        HttpURLConnection connection = null;
        try {
            connection = (HttpURLConnection) network.openConnection(new URL("http://" + ip + path));
            connection.setConnectTimeout(HTTP_CONNECT_TIMEOUT_MS);
            connection.setReadTimeout(HTTP_READ_TIMEOUT_MS);
            connection.setRequestMethod("GET");
            connection.setUseCaches(false);
            connection.setRequestProperty("Accept", "application/json");
            if (connection.getResponseCode() != 200) return null;
            try (InputStream input = connection.getInputStream();
                 BufferedReader reader = new BufferedReader(new InputStreamReader(input, StandardCharsets.UTF_8))) {
                StringBuilder result = new StringBuilder();
                char[] buffer = new char[2048];
                int n;
                while ((n = reader.read(buffer)) != -1) {
                    result.append(buffer, 0, n);
                    if (result.length() > 32768) return null;
                }
                return new JSONObject(result.toString());
            }
        } catch (IOException | JSONException | SecurityException e) {
            return null;
        } finally {
            if (connection != null) connection.disconnect();
        }
    }
}
