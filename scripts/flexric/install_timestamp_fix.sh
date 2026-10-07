#!/usr/bin/env bash
set -euo pipefail

if [ "$(id -u)" -ne 0 ]; then
  printf 'Root is required to install under /usr/local.\n' >&2
  exit 1
fi

fix_build=/home/nyu/flexric/build-ocudu
fix_backup=/usr/local/share/flexric/backups/kpm-timestamp-20261007-$(date +%H%M%S)-$$
mkdir -p "$fix_backup"
cp -p /usr/local/lib/flexric/libkpm_sm.so "$fix_backup/libkpm_sm.so"
cp -p /usr/local/bin/flexric/xApp/c/xapp_oran_moni "$fix_backup/xapp_oran_moni"
cp -p /usr/local/bin/flexric/xApp/c/xapp_all_moni "$fix_backup/xapp_all_moni"

# Rename new inodes into place so running processes retain their old mappings.
install -m 0644 "$fix_build/src/sm/kpm_sm/kpm_sm_v03.00/libkpm_sm.so" /usr/local/lib/flexric/.libkpm_sm.timestamp-new
mv -f /usr/local/lib/flexric/.libkpm_sm.timestamp-new /usr/local/lib/flexric/libkpm_sm.so
for fix_app in xapp_oran_moni xapp_all_moni; do
  install -m 0755 "$fix_build/examples/xApp/c/monitor/$fix_app" "/usr/local/bin/flexric/xApp/c/.$fix_app.timestamp-new"
  mv -f "/usr/local/bin/flexric/xApp/c/.$fix_app.timestamp-new" "/usr/local/bin/flexric/xApp/c/$fix_app"
done
printf 'Installed KPM timestamp fix; backup=%s\n' "$fix_backup"
