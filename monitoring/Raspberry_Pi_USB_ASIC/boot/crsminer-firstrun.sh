#!/bin/bash
# CRSminer Pi - one-time installer. Runs once at the first boot (see README), then reboots.
for d in /boot/firmware /boot; do [ -f "$d/crsminer.txt" ] && BOOT="$d" && break; done
cp "$BOOT/crsminer-run.sh" /usr/local/bin/crsminer-run.sh
cp "$BOOT/crsminer-status.py" /usr/local/bin/crsminer-status.py
chmod +x /usr/local/bin/crsminer-run.sh /usr/local/bin/crsminer-status.py
cat > /etc/systemd/system/crsminer.service <<'UNIT'
[Unit]
Description=CRSminer Pi (Block Erupter)
After=network.target
Wants=network.target

[Service]
ExecStart=/usr/local/bin/crsminer-run.sh
Restart=always
RestartSec=20

[Install]
WantedBy=multi-user.target
UNIT
systemctl enable crsminer.service
# remove the one-time hook from cmdline.txt so this does not run again
sed -i 's| systemd.run=[^ ]*||; s| systemd.run_success_action=[^ ]*||; s| systemd.unit=kernel-command-line.target||' "$BOOT/cmdline.txt"
rm -f "$BOOT/crsminer-firstrun.sh"
exit 0
