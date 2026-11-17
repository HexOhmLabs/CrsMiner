package com.hexohm.crsmining;

import android.content.Context;
import android.net.ConnectivityManager;
import android.net.LinkAddress;
import android.net.LinkProperties;
import android.net.Network;
import android.net.NetworkCapabilities;

import java.net.Inet4Address;
import java.net.InetAddress;
import java.util.ArrayList;
import java.util.List;

/** Scans the connected Wi-Fi/Ethernet IPv4 subnet only; never explores Internet IP ranges. */
final class NetworkLocator {
    static final int MAX_SCAN_ADDRESSES = 1024;

    static final class Selection {
        final Network network;
        final List<String> addresses;
        final String label;

        Selection(Network network, List<String> addresses, String label) {
            this.network = network;
            this.addresses = addresses;
            this.label = label;
        }
    }

    static Selection find(Context context) {
        ConnectivityManager cm = (ConnectivityManager) context.getSystemService(Context.CONNECTIVITY_SERVICE);
        if (cm == null) return null;
        Network[] networks = cm.getAllNetworks();
        // Prefer Wi-Fi, then Ethernet. Never use a cellular IP address to sweep a public network.
        for (int transport : new int[]{NetworkCapabilities.TRANSPORT_WIFI, NetworkCapabilities.TRANSPORT_ETHERNET}) {
            for (Network network : networks) {
                NetworkCapabilities capabilities = cm.getNetworkCapabilities(network);
                if (capabilities == null || !capabilities.hasTransport(transport)) continue;
                LinkProperties properties = cm.getLinkProperties(network);
                if (properties == null) continue;
                for (LinkAddress link : properties.getLinkAddresses()) {
                    InetAddress addr = link.getAddress();
                    if (!(addr instanceof Inet4Address) || addr.isLoopbackAddress() || addr.isLinkLocalAddress()) continue;
                    int prefix = link.getPrefixLength();
                    if (prefix < 8 || prefix > 30) continue;
                    List<String> hosts = hostsOnSubnet((Inet4Address) addr, prefix);
                    if (hosts.isEmpty()) continue;
                    long ip = toLong(addr.getAddress());
                    long mask = mask(prefix);
                    String name = transport == NetworkCapabilities.TRANSPORT_WIFI ? "Wi-Fi" : "Ethernet";
                    String description = name + " · " + ipToString(ip & mask) + "/" + prefix;
                    if (prefix < 22) description += " · scanning nearby 1024 IPs";
                    return new Selection(network, hosts, description);
                }
            }
        }
        return null;
    }

    static List<String> hostsOnSubnet(Inet4Address address, int prefix) {
        long ip = toLong(address.getAddress());
        long mask = mask(prefix);
        long first = (ip & mask) + 1;
        long last = (ip | (~mask & 0xffffffffL)) - 1;
        if (last < first) return new ArrayList<>();
        // Large corporate subnets: only inspect a /22 window containing this device.
        if (last - first + 1 > MAX_SCAN_ADDRESSES) {
            long segment = ip & 0xfffffc00L;
            first = Math.max(first, segment);
            last = Math.min(last, segment + 1023);
        }
        List<String> result = new ArrayList<>();
        // Include this phone so the monitor can find CRSminer for Android when
        // both apps are installed on the same device. The old loop started at
        // step 1 and silently excluded the local device's own Wi-Fi address.
        if (ip >= first && ip <= last) result.add(ipToString(ip));
        // Visit nearby hosts first, but cover the entire chosen range.
        for (long step = 1; result.size() < last - first + 1; step++) {
            long plus = ip + step;
            if (plus >= first && plus <= last) result.add(ipToString(plus));
            long minus = ip - step;
            if (minus >= first && minus <= last) result.add(ipToString(minus));
            if (plus > last && minus < first) break;
        }
        return result;
    }

    private static long mask(int prefix) {
        return (0xffffffffL << (32 - prefix)) & 0xffffffffL;
    }
    private static long toLong(byte[] ip) {
        return ((ip[0] & 255L) << 24) | ((ip[1] & 255L) << 16)
                | ((ip[2] & 255L) << 8) | (ip[3] & 255L);
    }
    private static String ipToString(long ip) {
        return ((ip >> 24) & 255) + "." + ((ip >> 16) & 255) + "."
                + ((ip >> 8) & 255) + "." + (ip & 255);
    }
}
