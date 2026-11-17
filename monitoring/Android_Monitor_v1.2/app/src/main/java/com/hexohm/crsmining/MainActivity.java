package com.hexohm.crsmining;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.ClipData;
import android.content.ClipboardManager;
import android.content.Context;
import android.content.Intent;
import android.content.SharedPreferences;
import android.content.pm.PackageManager;
import android.graphics.Color;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.view.Gravity;
import android.view.Menu;
import android.view.MenuItem;
import android.view.View;
import android.widget.Button;
import android.widget.GridLayout;
import android.widget.LinearLayout;
import android.widget.PopupMenu;
import android.widget.ScrollView;
import android.widget.TextView;
import android.widget.Toast;

import java.util.ArrayList;
import java.util.Comparator;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.Set;
import java.util.HashSet;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.atomic.AtomicInteger;

/**
 * CRS Mining v1.2 — local-only, read-only fleet monitor for CRSminer-8.
 * Displays firmware telemetry, never edits miner settings or accesses any cloud service.
 */
public final class MainActivity extends Activity {
    private static final String PREFS = "crs_mining_prefs";
    private static final String PERMISSION_LAN = "android.permission.ACCESS_LOCAL_NETWORK";
    private static final int REQUEST_LAN = 1231;
    private static final long REFRESH_INTERVAL_MS = 5000L;
    private static final long CONFIG_INTERVAL_MS = 60000L;

    private final ExecutorService scanExecutor = Executors.newFixedThreadPool(24);
    private final ExecutorService pollExecutor = Executors.newFixedThreadPool(8);
    private final Handler main = new Handler(Looper.getMainLooper());
    private final AtomicInteger scanGeneration = new AtomicInteger();
    private final Map<String, MinerData> miners = new LinkedHashMap<>();
    private SharedPreferences preferences;
    private NetworkLocator.Selection network;
    private boolean running;
    private boolean scanning;
    private int columns;
    private boolean dark;
    private int scanned;
    private int scanTotal;

    private LinearLayout root;
    private LinearLayout feed;
    private TextView statusLine;
    private TextView aggregateRate;
    private TextView aggregateSummary;
    private TextView aggregateShares;
    private GridLayout tiles;

    private int surface() { return dark ? Color.rgb(11, 18, 32) : Color.rgb(241, 245, 250); }
    private int cardBg() { return dark ? Color.rgb(20, 32, 49) : Color.WHITE; }
    private int fg() { return dark ? Color.rgb(240, 247, 255) : Color.rgb(22, 37, 57); }
    private int muted() { return dark ? Color.rgb(153, 170, 195) : Color.rgb(92, 111, 138); }
    private int stroke() { return dark ? Color.rgb(44, 62, 87) : Color.rgb(216, 226, 238); }
    private int blue() { return Color.rgb(46, 170, 255); }
    private int green() { return dark ? Color.rgb(70, 218, 155) : Color.rgb(0, 134, 83); }
    private int amber() { return dark ? Color.rgb(255, 194, 80) : Color.rgb(180, 115, 10); }
    private int red() { return dark ? Color.rgb(255, 103, 114) : Color.rgb(192, 44, 65); }

    @Override public void onCreate(Bundle state) {
        preferences = getSharedPreferences(PREFS, MODE_PRIVATE);
        dark = preferences.getBoolean("dark", true);
        setTheme(dark ? R.style.AppThemeDark : R.style.AppThemeLight);
        super.onCreate(state);
        columns = preferences.getInt("columns", 3);
        if (columns < 2 || columns > 4) columns = 3;
        buildUi();
    }

    @Override protected void onResume() {
        super.onResume();
        running = true;
        if (hasLanPermission()) beginDiscovery();
        else requestLanPermission();
        main.postDelayed(refreshLoop, REFRESH_INTERVAL_MS);
    }

    @Override protected void onPause() {
        running = false;
        scanGeneration.incrementAndGet();
        scanning = false;
        main.removeCallbacks(refreshLoop);
        super.onPause();
    }

    @Override protected void onDestroy() {
        scanExecutor.shutdownNow();
        pollExecutor.shutdownNow();
        super.onDestroy();
    }

    private boolean hasLanPermission() {
        return Build.VERSION.SDK_INT < 37 || checkSelfPermission(PERMISSION_LAN) == PackageManager.PERMISSION_GRANTED;
    }

    private void requestLanPermission() {
        if (Build.VERSION.SDK_INT >= 37) {
            statusLine.setText("Allow local network access to discover CRSminers.");
            requestPermissions(new String[] {PERMISSION_LAN}, REQUEST_LAN);
        }
    }

    @Override public void onRequestPermissionsResult(int code, String[] permissions, int[] results) {
        super.onRequestPermissionsResult(code, permissions, results);
        if (code != REQUEST_LAN) return;
        if (results.length > 0 && results[0] == PackageManager.PERMISSION_GRANTED) beginDiscovery();
        else statusLine.setText("Local network permission denied. Grant permission in Android Settings, then tap ↻.");
    }

    private final Runnable refreshLoop = new Runnable() {
        @Override public void run() {
            if (!running) return;
            if (hasLanPermission()) refreshKnown();
            main.postDelayed(this, REFRESH_INTERVAL_MS);
        }
    };

    private void beginDiscovery() {
        if (!running || !hasLanPermission()) return;
        // Running network discovery is intentionally limited to attached Wi-Fi/Ethernet subnets.
        NetworkLocator.Selection next = NetworkLocator.find(this);
        if (next == null) {
            statusLine.setText("Connect your phone to the same Wi-Fi as the CRSminers, then tap ↻.");
            return;
        }
        network = next;
        final int token = scanGeneration.incrementAndGet();
        scanning = true;
        scanned = 0;
        scanTotal = network.addresses.size();
        statusLine.setText("Scanning " + network.label + " · 0/" + scanTotal);

        // Check previously discovered IP addresses immediately; a sweep handles DHCP changes.
        Set<String> remembered = preferences.getStringSet("known_ips", new HashSet<>());
        for (String ip : new HashSet<>(remembered)) {
            if (network.addresses.contains(ip)) pollSingle(ip, network, true);
        }
        AtomicInteger counter = new AtomicInteger();
        final NetworkLocator.Selection onNetwork = network;
        for (String ip : onNetwork.addresses) {
            scanExecutor.execute(() -> {
                if (scanGeneration.get() != token || !running) return;
                MinerData result = MinerClient.discover(onNetwork.network, ip);
                if (result != null) {
                    MinerClient.Config cfg = MinerClient.readConfig(onNetwork.network, ip);
                    if (cfg != null) setConfig(result, cfg);
                    main.post(() -> {
                        if (token != scanGeneration.get() || !running) return;
                        accept(result);
                    });
                }
                int done = counter.incrementAndGet();
                if (done == scanTotal || done % 24 == 0) main.post(() -> {
                    if (scanGeneration.get() != token || !running) return;
                    scanned = done;
                    scanning = done < scanTotal;
                    updateStatusLine();
                });
            });
        }
    }

    private void setConfig(MinerData data, MinerClient.Config cfg) {
        data.worker = cfg.worker;
        data.pool = cfg.pool;
        data.wallet = cfg.wallet;
        data.lastConfigReadMs = android.os.SystemClock.elapsedRealtime();
    }

    private void accept(MinerData fresh) {
        MinerData existing = miners.get(fresh.ip);
        if (existing == null) {
            fresh.history.addLast(Math.max(0, fresh.hashesPerSecond));
            miners.put(fresh.ip, fresh);
        } else {
            existing.updateTelemetry(fresh);
            if (!fresh.worker.isEmpty()) existing.worker = fresh.worker;
            if (!fresh.wallet.isEmpty()) existing.wallet = fresh.wallet;
            if (!fresh.pool.isEmpty()) existing.pool = fresh.pool;
            if (fresh.lastConfigReadMs > 0) existing.lastConfigReadMs = fresh.lastConfigReadMs;
        }
        rememberIPs();
        render();
    }

    private void rememberIPs() {
        // Network configuration and wallet addresses are never persisted.
        Set<String> addresses = new HashSet<>(miners.keySet());
        preferences.edit().putStringSet("known_ips", addresses).apply();
    }

    private void refreshKnown() {
        NetworkLocator.Selection current = NetworkLocator.find(this);
        if (current == null) {
            for (MinerData m : miners.values()) m.reachable = false;
            network = null;
            render();
            statusLine.setText("No Wi-Fi/Ethernet network. Monitoring paused.");
            return;
        }
        if (network == null || !network.label.equals(current.label)) {
            beginDiscovery();
            return;
        }
        for (String ip : new ArrayList<>(miners.keySet())) pollSingle(ip, current, false);
    }

    private void pollSingle(String ip, NetworkLocator.Selection net, boolean fetchConfigNow) {
        MinerData previous = miners.get(ip);
        if (previous != null && !previous.polling.compareAndSet(false, true)) return;
        pollExecutor.execute(() -> {
            try {
                MinerData updated = MinerClient.status(net.network, ip);
                MinerClient.Config cfg = null;
                if (updated != null && (fetchConfigNow || previous == null ||
                        android.os.SystemClock.elapsedRealtime() - previous.lastConfigReadMs > CONFIG_INTERVAL_MS)) {
                    cfg = MinerClient.readConfig(net.network, ip);
                }
                if (updated != null && cfg != null) setConfig(updated, cfg);
                final MinerData newer = updated;
                main.post(() -> {
                    if (!running) return;
                    if (newer != null) accept(newer);
                    else {
                        MinerData miner = miners.get(ip);
                        if (miner != null && ++miner.missedPolls >= 2) miner.reachable = false;
                        render();
                    }
                });
            } finally {
                if (previous != null) previous.polling.set(false);
            }
        });
    }

    private void buildUi() {
        getWindow().setStatusBarColor(surface());
        getWindow().setNavigationBarColor(surface());
        getWindow().getDecorView().setSystemUiVisibility(dark ? 0 : View.SYSTEM_UI_FLAG_LIGHT_STATUS_BAR | View.SYSTEM_UI_FLAG_LIGHT_NAVIGATION_BAR);
        root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setBackgroundColor(surface());

        LinearLayout top = horizontal(Gravity.CENTER_VERTICAL);
        top.setPadding(dp(18), dp(15), dp(14), dp(13));
        LinearLayout brand = vertical();
        TextView title = label("CRS Mining", 23, fg(), true);
        TextView subtitle = label("LOCAL MINER MONITOR", 10, muted(), true);
        subtitle.setLetterSpacing(.13f);
        brand.addView(title);
        brand.addView(subtitle);
        top.addView(brand, new LinearLayout.LayoutParams(0, -2, 1));
        TextView refresh = label("↻", 29, blue(), true);
        refresh.setGravity(Gravity.CENTER);
        refresh.setContentDescription("Scan network again");
        top.addView(refresh, new LinearLayout.LayoutParams(dp(45), dp(45)));
        refresh.setOnClickListener(v -> {
            if (hasLanPermission()) beginDiscovery();
            else requestLanPermission();
        });
        TextView more = label("⋮", 29, fg(), true);
        more.setGravity(Gravity.CENTER);
        more.setContentDescription("More options");
        top.addView(more, new LinearLayout.LayoutParams(dp(42), dp(45)));
        more.setOnClickListener(v -> showMenu(more));
        root.addView(top);

        ScrollView scroll = new ScrollView(this);
        scroll.setFillViewport(true);
        scroll.setClipToPadding(false);
        feed = vertical();
        feed.setPadding(dp(14), dp(5), dp(14), dp(28));
        scroll.addView(feed, new ScrollView.LayoutParams(-1, -2));
        root.addView(scroll, new LinearLayout.LayoutParams(-1, 0, 1));

        LinearLayout overview = vertical();
        overview.setPadding(dp(18), dp(18), dp(18), dp(16));
        overview.setBackground(makeBackground(cardBg(), stroke(), 18));
        TextView caption = label("TOTAL FLEET HASHRATE", 11, muted(), true);
        caption.setLetterSpacing(.13f);
        overview.addView(caption);
        aggregateRate = label("— H/s", 31, fg(), true);
        overview.addView(aggregateRate);
        aggregateSummary = label("Looking for CRSminers", 12, muted(), false);
        overview.addView(aggregateSummary);
        aggregateShares = label("ACCEPTED  0     REJECTED  0", 12, muted(), true);
        LinearLayout.LayoutParams slp = new LinearLayout.LayoutParams(-1, -2);
        slp.topMargin = dp(10);
        overview.addView(aggregateShares, slp);
        feed.addView(overview);

        statusLine = label("Preparing local network scan…", 12, muted(), false);
        statusLine.setPadding(dp(3), dp(14), dp(3), dp(12));
        feed.addView(statusLine);

        TextView header = label("MINERS", 13, fg(), true);
        header.setLetterSpacing(.14f);
        header.setPadding(dp(3), dp(7), dp(3), dp(11));
        feed.addView(header);
        tiles = new GridLayout(this);
        tiles.setColumnCount(columns);
        tiles.setOrientation(GridLayout.HORIZONTAL);
        feed.addView(tiles);
        setContentView(root);
        render();
    }

    private void showMenu(View anchor) {
        PopupMenu popup = new PopupMenu(this, anchor);
        Menu menu = popup.getMenu();
        menu.add(0, 1, 0, "☀  Day mode").setCheckable(true).setChecked(!dark);
        menu.add(0, 2, 1, "☾  Night mode").setCheckable(true).setChecked(dark);
        menu.add(0, 10, 2, "2 tiles per row").setCheckable(true).setChecked(columns == 2);
        menu.add(0, 11, 3, "3 tiles per row").setCheckable(true).setChecked(columns == 3);
        menu.add(0, 12, 4, "4 tiles per row").setCheckable(true).setChecked(columns == 4);
        menu.add(0, 99, 5, "About");
        popup.setOnMenuItemClickListener(this::menuSelected);
        popup.show();
    }

    private boolean menuSelected(MenuItem item) {
        int id = item.getItemId();
        if (id == 99) {
            new AlertDialog.Builder(this)
                    .setTitle("CRS Mining")
                    .setMessage("Version 1.2 · 2026\nby HexOhm\n\nA local-network monitor for CRSminer ESP32 / ESP32-S3 devices.\n\nThe app reads hash rate, accepted/rejected shares, pool, coin wallet address, and diagnostics from the miner's built-in HTTP API. No cloud service or mining controls.")
                    .setPositiveButton("OK", null).show();
            return true;
        }
        if (id == 1 || id == 2) {
            dark = id == 2;
            preferences.edit().putBoolean("dark", dark).apply();
            recreate();  // Also update Android's native popup/dialog theme.
            return true;
        }
        if (id >= 10 && id <= 12) {
            columns = id - 8;
            preferences.edit().putInt("columns", columns).apply();
            buildUi();
            updateStatusLine();
            return true;
        }
        return false;
    }

    private void updateStatusLine() {
        if (statusLine == null) return;
        if (network == null) {
            statusLine.setText("Connect to the miners' Wi-Fi network.");
            return;
        }
        String label = scanning ? "Scanning " + network.label + " · " + scanned + "/" + scanTotal
                : network.label + " · refreshed every 5 seconds";
        statusLine.setText(label);
    }

    private void render() {
        if (tiles == null) return;
        double total = 0;
        long good = 0, bad = 0;
        int online = 0;
        List<MinerData> list = new ArrayList<>(miners.values());
        list.sort(Comparator.comparing(a -> a.hostname == null ? a.ip : a.hostname));
        for (MinerData m : list) {
            if (m.reachable) {
                online++;
                total += m.hashesPerSecond;
                good += m.accepted;
                bad += m.rejected;
            }
        }
        String[] rate = HashFormat.parts(total);
        aggregateRate.setText(rate[0] + " " + rate[1]);
        aggregateSummary.setText(online + " online / " + list.size() + " miners found");
        aggregateShares.setText("✓ " + good + " accepted    ✕ " + bad + " rejected");
        tiles.removeAllViews();
        if (list.isEmpty()) {
            TextView empty = label(scanning ? "Scanning your local network for CRSminers…" :
                    "No CRSminers found. Check that your phone is on the same Wi-Fi, then tap ↻.", 13, muted(), false);
            empty.setPadding(dp(8), dp(20), dp(8), dp(20));
            GridLayout.LayoutParams ep = new GridLayout.LayoutParams(GridLayout.spec(0), GridLayout.spec(0, columns));
            ep.width = -1;
            tiles.addView(empty, ep);
            return;
        }
        int windowWidth = getResources().getDisplayMetrics().widthPixels;
        int gap = dp(9);
        int cardWidth = Math.max(dp(70), (windowWidth - dp(28) - gap * columns) / columns);
        for (int i = 0; i < list.size(); i++) {
            MinerData m = list.get(i);
            GridLayout.LayoutParams lp = new GridLayout.LayoutParams(
                    GridLayout.spec(i / columns), GridLayout.spec(i % columns));
            lp.width = cardWidth;
            lp.height = -2;
            lp.setMargins(0, 0, gap, gap);
            tiles.addView(makeCard(m), lp);
        }
    }

    private View makeCard(MinerData m) {
        int health = healthColor(m);
        LinearLayout outer = vertical();
        int pad = columns == 4 ? dp(8) : dp(11);
        outer.setPadding(pad, dp(12), pad, dp(12));
        outer.setBackground(makeBackground(cardBg(), stroke(), 14));
        outer.setMinimumHeight(dp(columns == 4 ? 240 : 254));
        outer.setContentDescription((m.hostname == null ? m.ip : m.hostname) + ", worker " + m.worker);

        TextView indicator = label("● " + healthText(m), columns == 4 ? 9 : 10, health, true);
        indicator.setMaxLines(1);
        indicator.setEllipsize(android.text.TextUtils.TruncateAt.END);
        outer.addView(indicator);

        TextView hostname = label(m.hostname == null ? m.ip : m.hostname,
                columns == 4 ? 9 : columns == 3 ? 10 : 12, fg(), true);
        hostname.setMaxLines(2);
        hostname.setBreakStrategy(android.text.Layout.BREAK_STRATEGY_SIMPLE);
        hostname.setEllipsize(android.text.TextUtils.TruncateAt.MIDDLE);
        LinearLayout.LayoutParams hlp = new LinearLayout.LayoutParams(-1, dp(columns == 4 ? 29 : 34));
        hlp.topMargin = dp(8);
        outer.addView(hostname, hlp);

        TextView worker = label("· " + (m.worker.isEmpty() ? "worker unknown" : m.worker),
                columns == 4 ? 9 : 11, muted(), false);
        worker.setSingleLine();
        worker.setEllipsize(android.text.TextUtils.TruncateAt.END);
        outer.addView(worker);

        TextView rateCaption = label("HASHRATE", 9, muted(), true);
        LinearLayout.LayoutParams rclp = new LinearLayout.LayoutParams(-1, -2);
        rclp.topMargin = dp(12);
        outer.addView(rateCaption, rclp);
        String[] rateParts = HashFormat.parts(m.hashesPerSecond);
        TextView big = label(m.reachable ? rateParts[0] : "—", columns == 2 ? 36 : columns == 3 ? 31 : 23, fg(), true);
        big.setSingleLine();
        big.setAutoSizeTextTypeUniformWithConfiguration(columns == 4 ? 17 : 20,
                columns == 4 ? 26 : 37, 1, android.util.TypedValue.COMPLEX_UNIT_SP);
        outer.addView(big, new LinearLayout.LayoutParams(-1, dp(columns == 4 ? 35 : 45)));
        TextView unit = label(m.reachable ? rateParts[1] : "OFFLINE", 11, health, true);
        outer.addView(unit);

        SparklineView trend = new SparklineView(this, m.history, health);
        LinearLayout.LayoutParams trendLp = new LinearLayout.LayoutParams(-1, dp(34));
        trendLp.topMargin = dp(5);
        trendLp.bottomMargin = dp(10);
        outer.addView(trend, trendLp);

        LinearLayout counts = horizontal(Gravity.CENTER_VERTICAL);
        TextView accepted = label("✓ " + m.accepted, columns == 4 ? 10 : 12, green(), true);
        TextView rejected = label("✕ " + m.rejected, columns == 4 ? 10 : 12, red(), true);
        counts.addView(accepted, new LinearLayout.LayoutParams(0, -2, 1));
        counts.addView(rejected);
        outer.addView(counts);

        TextView poolTag = label("POOL", 9, muted(), true);
        LinearLayout.LayoutParams ptp = new LinearLayout.LayoutParams(-1, -2);
        ptp.topMargin = dp(13);
        outer.addView(poolTag, ptp);
        String pool = m.pool.isEmpty() ? "—" : m.pool;
        TextView poolText = label(pool, columns == 4 ? 9 : 10, fg(), false);
        poolText.setSingleLine();
        poolText.setEllipsize(android.text.TextUtils.TruncateAt.MIDDLE);
        outer.addView(poolText);

        TextView walletTag = label("COIN ADDRESS", 9, muted(), true);
        LinearLayout.LayoutParams wtp = new LinearLayout.LayoutParams(-1, -2);
        wtp.topMargin = dp(8);
        outer.addView(walletTag, wtp);
        TextView walletText = label(m.wallet.isEmpty() ? "—" : m.wallet,
                columns == 4 ? 9 : 10, fg(), false);
        walletText.setSingleLine();
        walletText.setEllipsize(android.text.TextUtils.TruncateAt.MIDDLE);
        outer.addView(walletText);

        outer.setOnClickListener(v -> showMinerDetails(m));
        outer.setOnLongClickListener(v -> {
            if (!m.wallet.isEmpty()) {
                copy("Coin wallet address", m.wallet);
                return true;
            }
            return false;
        });
        return outer;
    }

    private int healthColor(MinerData m) {
        if (!m.reachable) return red();
        if (m.diagnostic.toLowerCase(Locale.ROOT).contains("self-test") ||
                m.diagnostic.toLowerCase(Locale.ROOT).contains("refused")) return red();
        // A saved Stratum error (such as "job not found") is historical, not a live fault.
        // Active diagnostics, pool connectivity and hashing still determine health.
        if (!m.diagnostic.isEmpty() || !m.poolReady || m.hashesPerSecond <= 0) return amber();
        return green();
    }

    private String healthText(MinerData m) {
        if (!m.reachable) return "OFFLINE";
        if (!m.diagnostic.isEmpty()) return "WARNING";
        if (!m.poolReady) return "POOL OFFLINE";
        if (m.hashesPerSecond <= 0) return "NOT HASHING";
        return "MINING";
    }

    private void showMinerDetails(MinerData m) {
        String url = "http://" + m.ip + "/";
        String contents = "URL: " + (m.hostname == null ? m.ip : m.hostname) + "\n"
                + "Worker: " + fallback(m.worker) + "\n"
                + "IP: " + m.ip + "\n"
                + "Status: " + healthText(m) + "\n\n"
                + "Hashrate: " + String.join(" ", HashFormat.parts(m.hashesPerSecond)) + "\n"
                + "Accepted shares: " + m.accepted + "\n"
                + "Rejected shares: " + m.rejected + "\n"
                + "Best difficulty: " + diff(m.bestDifficulty) + "\n"
                + "Pool difficulty: " + diff(m.poolDifficulty) + "\n\n"
                + "Pool: " + fallback(m.pool) + "\n"
                + "Coin address: " + fallback(m.wallet) + "\n\n"
                + "Uptime: " + uptime(m.uptimeSeconds) + "\n"
                + "Wi-Fi RSSI: " + m.rssi + " dBm\n"
                + "ESP32 reported temperature: " + String.format(Locale.US, "%.1f°C", m.chipTemperature) + "\n"
                + "Jobs: " + m.jobs + "\n"
                + "SHA: " + fallback(m.shaMode) + "\n"
                + "Stage: " + fallback(m.stage) + "\n"
                + "Diagnostic: " + fallback(m.diagnostic) + "\n"
                + "Last error: " + fallback(m.error);
        new AlertDialog.Builder(this)
                .setTitle(m.hostname == null ? "CRSminer" : m.hostname)
                .setMessage(contents)
                .setPositiveButton("Open web dashboard", (dialog, which) -> {
                    Intent intent = new Intent(Intent.ACTION_VIEW, Uri.parse(url));
                    try { startActivity(intent); }
                    catch (Exception ex) { Toast.makeText(this, "No browser available", Toast.LENGTH_SHORT).show(); }
                })
                .setNeutralButton("Copy coin address", (dialog, which) -> {
                    if (!m.wallet.isEmpty()) copy("Coin wallet address", m.wallet);
                })
                .setNegativeButton("Close", null).show();
    }

    private void copy(String title, String value) {
        ClipboardManager cm = (ClipboardManager) getSystemService(Context.CLIPBOARD_SERVICE);
        if (cm != null) {
            cm.setPrimaryClip(ClipData.newPlainText(title, value));
            Toast.makeText(this, title + " copied", Toast.LENGTH_SHORT).show();
        }
    }

    private static String fallback(String value) { return value == null || value.isEmpty() ? "—" : value; }
    private static String diff(double d) {
        if (!(d > 0) || !Double.isFinite(d)) return "—";
        return d >= 1000 ? String.join(" ", HashFormat.parts(d)).replace("H/s", "") : String.format(Locale.US, "%.5g", d);
    }
    private static String uptime(long s) {
        return (s / 86400) + "d " + ((s % 86400) / 3600) + "h " + ((s % 3600) / 60) + "m";
    }

    private GradientDrawable makeBackground(int bg, int line, int radius) {
        GradientDrawable gd = new GradientDrawable();
        gd.setColor(bg);
        gd.setCornerRadius(dp(radius));
        gd.setStroke(dp(1), line);
        return gd;
    }
    private int dp(float v) { return Math.round(v * getResources().getDisplayMetrics().density); }
    private LinearLayout vertical() { LinearLayout v = new LinearLayout(this); v.setOrientation(LinearLayout.VERTICAL); return v; }
    private LinearLayout horizontal(int gravity) { LinearLayout v = new LinearLayout(this); v.setOrientation(LinearLayout.HORIZONTAL); v.setGravity(gravity); return v; }
    private TextView label(String text, float size, int color, boolean bold) {
        TextView view = new TextView(this);
        view.setText(text);
        view.setTextSize(size);
        view.setTextColor(color);
        if (bold) view.setTypeface(Typeface.DEFAULT, Typeface.BOLD);
        view.setIncludeFontPadding(true);
        return view;
    }
}
