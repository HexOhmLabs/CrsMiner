#!/bin/bash
# CRSminer Pi - runs at every boot (started by crsminer.service). By HexOhm, MIT.
# Reads crsminer.txt from the SD card boot partition, makes sure WiFi/hostname are set,
# makes sure the miner program exists (builds it once if needed), then starts it.

for d in /boot/firmware /boot; do [ -f "$d/crsminer.txt" ] && BOOT="$d" && break; done
[ -n "$BOOT" ] || { echo "crsminer.txt not found on the boot partition"; exit 1; }
CFG="$BOOT/crsminer.txt"
LOG=/var/log/crsminer-setup.log
say() { echo "$(date '+%F %T') $*" | tee -a "$LOG"; }

# ---- read settings (key=value, tolerate Windows line endings and spaces) ----
get() { grep -E "^[[:space:]]*$1[[:space:]]*=" "$CFG" | tail -1 | sed -E "s/^[^=]*=//; s/\r$//; s/^[[:space:]]+//; s/[[:space:]]+$//"; }
SSID=$(get wifi_ssid); PASS=$(get wifi_password); COUNTRY=$(get wifi_country)
NAME=$(get name); POOL=$(get pool); WALLET=$(get wallet); WORKER=$(get worker)
PPASS=$(get pool_password); LANGV=$(get lang); EXTRA=$(get extra_args)
[ -z "$PPASS" ] && PPASS=x; [ -z "$WORKER" ] && WORKER=erupter; [ -z "$LANGV" ] && LANGV=en

# ---- validate (a bad file must give a clear message, not a silent failure) ----
bad() { say "SETTINGS ERROR: $1"; echo "$1" > /run/crsminer-error; exit 0; }
rm -f /run/crsminer-error
[[ "$NAME" =~ ^[A-Za-z0-9]{4}$ ]] || bad "name must be exactly 4 letters/digits"
[[ "$WALLET" =~ ^[A-Za-z0-9]{20,100}$ ]] || bad "wallet address missing or invalid"
[[ "$WORKER" =~ ^[A-Za-z0-9_-]{1,32}$ ]] || bad "worker: letters/digits/_/- only"
[[ "$POOL" =~ ^(stratum\+tcp://)?[A-Za-z0-9.-]+:[0-9]{2,5}$ ]] || bad "pool must look like host:port"
[[ "$PPASS" =~ ^[^[:space:]\'\"\\]+$ ]] || bad "pool_password has forbidden characters"
POOL="${POOL#stratum+tcp://}"

# ---- hostname ----
HN="crsminer-$(echo "$NAME" | tr 'A-Z' 'a-z')"
if [ "$(hostname)" != "$HN" ]; then
  hostnamectl set-hostname "$HN" 2>/dev/null || hostname "$HN"
  sed -i "s/127\.0\.1\.1.*/127.0.1.1\t$HN/" /etc/hosts
  systemctl restart avahi-daemon 2>/dev/null
fi

# ---- WiFi (only re-applied when the file's WiFi lines changed) ----
SUM=$(printf '%s|%s|%s' "$SSID" "$PASS" "$COUNTRY" | sha256sum | cut -d' ' -f1)
if [ "$SUM" != "$(cat /var/lib/crsminer-wifi 2>/dev/null)" ] && [ -n "$SSID" ]; then
  say "applying WiFi settings for '$SSID'"
  [ -n "$COUNTRY" ] && { raspi-config nonint do_wifi_country "$COUNTRY" 2>/dev/null || iw reg set "$COUNTRY" 2>/dev/null; }
  rfkill unblock wifi 2>/dev/null
  if command -v nmcli >/dev/null && systemctl is-active --quiet NetworkManager; then
    nmcli connection delete crsminer-wifi >/dev/null 2>&1
    if [ -n "$PASS" ]; then nmcli connection add type wifi con-name crsminer-wifi ifname wlan0 ssid "$SSID" wifi-sec.key-mgmt wpa-psk wifi-sec.psk "$PASS" connection.autoconnect yes >/dev/null
    else nmcli connection add type wifi con-name crsminer-wifi ifname wlan0 ssid "$SSID" connection.autoconnect yes >/dev/null; fi
    nmcli connection up crsminer-wifi >/dev/null 2>&1
  else   # older images: wpa_supplicant
    { echo "ctrl_interface=DIR=/var/run/wpa_supplicant GROUP=netdev"; echo "update_config=1"; echo "country=${COUNTRY:-GR}"
      echo "network={"; echo "  ssid=\"$SSID\""; [ -n "$PASS" ] && echo "  psk=\"$PASS\"" || echo "  key_mgmt=NONE"; echo "}"; } > /etc/wpa_supplicant/wpa_supplicant.conf
    chmod 600 /etc/wpa_supplicant/wpa_supplicant.conf
    wpa_cli -i wlan0 reconfigure >/dev/null 2>&1 || systemctl restart wpa_supplicant 2>/dev/null
  fi
  echo "$SUM" > /var/lib/crsminer-wifi
fi

# ---- wait for the network (up to 3 min) ----
for i in $(seq 1 90); do ping -c1 -W2 1.1.1.1 >/dev/null 2>&1 && break; sleep 2; done

# ---- miner program: build once (cgminer 3.7.2 = the classic Block Erupter miner) ----
if [ ! -x /usr/local/bin/cgminer ]; then
  say "miner not installed yet - building it (first boot only, can take 10-30 min on a Pi A+)"
  export DEBIAN_FRONTEND=noninteractive
  apt-get update >>"$LOG" 2>&1
  apt-get install -y build-essential git autoconf automake libtool pkg-config libcurl4-openssl-dev libusb-1.0-0-dev libncurses-dev libudev-dev >>"$LOG" 2>&1 || { say "apt install failed (no internet?) - will retry next boot"; exit 1; }
  rm -rf /opt/cgminer-src
  git clone --depth 1 --branch v3.7.2 https://github.com/ckolivas/cgminer /opt/cgminer-src >>"$LOG" 2>&1 || { say "download failed - will retry next boot"; exit 1; }
  cd /opt/cgminer-src || exit 1
  # some cgminer 3.x sources need the older, permissive compiler settings on current GCC
  ./autogen.sh --enable-icarus --disable-opencl --disable-adl CFLAGS="-O2 -Wno-error -fcommon" >>"$LOG" 2>&1 \
    && make -j1 >>"$LOG" 2>&1 && make install >>"$LOG" 2>&1 || { say "build failed - see $LOG"; exit 1; }
  say "miner built OK"
fi

# ---- status page (also speaks the same /api/status JSON as the ESP32 miners, so the Fleet pages can see it) ----
pkill -f crsminer-status.py 2>/dev/null
CRS_NAME="$HN" CRS_POOL="$POOL" CRS_LANG="$LANGV" nohup python3 /usr/local/bin/crsminer-status.py >/dev/null 2>&1 &

# ---- start mining (restarted by systemd if it ever exits) ----
say "starting miner: $POOL as ${WALLET:0:6}...$WORKER"
# Block Erupters are found automatically over USB; --text-only = no curses screen
exec /usr/local/bin/cgminer -T --api-listen --api-port 4028 \
  -o "stratum+tcp://$POOL" -u "$WALLET.$WORKER" -p "$PPASS" $EXTRA
