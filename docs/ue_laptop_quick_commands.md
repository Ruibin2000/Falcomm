# UE Laptop Quick Commands

Updated: 2026-10-07. Run these steps in order in **one Bash terminal on the UE laptop** and keep the variables defined below. Continue only after each step succeeds. For full instructions, see [Runbook stages 10–14](full_reproduction_runbook.md#10-prepare-a-new-ubuntulinux-laptop); start Spark using [Spark Quick Commands](spark_quick_commands.md).

The 2026-10-05 session verified single-UE registration, IPv4 connectivity, and an initial 10-second TCP uplink. The 2026-10-07 session verified five DU KPM measurements from a real UE. After the timestamp repair, the latest run received 12 reports for UE ID `7`, with `report_age_us` of **591–796 µs**, and the xApp exited normally. The ping and 30-second uplink/downlink commands below are further acceptance checks, not results demonstrated in this session.

## 1 Install tools and select the modem

Install these tools on a new laptop; skip this if they are already installed:

```bash
sudo apt update
sudo apt install -y modemmanager libqmi-utils minicom iproute2 iperf3 usbutils
sudo systemctl enable --now ModemManager
```

Connect the RM500Q-GL, test USIM, antennas, data USB, and auxiliary power to the RMU500EK. Verified firmware: `RM500QGLABR13A03M4G`. If ports are missing, see [Driver checks](software_installation_and_drivers.md#72-kernel-driver-responsibilities).

Define these value-extraction and modem-selection functions once. If several RM500Q modems are present, select the intended device explicitly:

```bash
mm_value() {
  awk -F ':' -v key="$1" '
    { field=$1; gsub(/^[ \t]+|[ \t]+$/, "", field) }
    field==key { sub(/^[^:]*:[ \t]*/, ""); sub(/[ \t]+$/, ""); print; exit }
  '
}

select_rm500q() {
  MODEM=""
  local listing model
  local -a candidates
  listing=$(mmcli -L) || return 1
  mapfile -t candidates < <(printf '%s\n' "$listing" |
    awk '/RM500Q/ { print }' | grep -oE '/org/freedesktop/ModemManager1/Modem/[0-9]+')
  case ${#candidates[@]} in
    0) printf 'No RM500Q found; check USB and drivers before retrying.\n'; return 1 ;;
    1) MODEM=${candidates[0]} ;;
    *) printf '%s\n' "$listing"; read -r -p 'Enter the full Modem path of the intended RM500Q: ' MODEM ;;
  esac
  MODEM_KV=$(mmcli -m "$MODEM" -K) || { MODEM=""; return 1; }
  model=$(printf '%s\n' "$MODEM_KV" | mm_value modem.generic.model)
  MODEM_IDENTITY=$(printf '%s\n' "$MODEM_KV" | mm_value modem.generic.device-identifier)
  if [[ "$model" != *RM500Q* ]] || [[ -z "$MODEM_IDENTITY" || "$MODEM_IDENTITY" = -- ]]; then
    MODEM=""; printf 'Model or device identifier does not match; stop here.\n'; return 1
  fi
  printf 'modem=%s model=%s firmware=%s\n' "$MODEM" "$model" \
    "$(printf '%s\n' "$MODEM_KV" | mm_value modem.generic.revision)"
}
select_rm500q
```

If the modem is disabled, run `sudo mmcli -m "$MODEM" --enable`. Rediscover modem, SIM, and bearer IDs instead of reusing historical numbers.

## 2 Configure NR SA and lock the private cell

If the settings are already correct and the modem is registered, continue to stage 3. Otherwise, select an **actual `(at)` port**:

```bash
AT_PORT=$(mmcli -m "$MODEM" | grep -oE 'ttyUSB[0-9]+ \(at\)' | head -1 | awk '{print $1}')
if [ -n "$AT_PORT" ]; then
  sudo minicom -D "/dev/$AT_PORT" -b 115200
else
  printf 'No AT port found; stop and inspect the modem ports.\n'
fi
```

Enter these commands in minicom, **not in Bash**. Each configuration write must return `OK`:

```text
AT
AT+QNWPREFCFG="mode_pref",NR5G
AT+QNWPREFCFG="mode_pref"
AT+QNWPREFCFG="nr5g_disable_mode",2
AT+QNWPREFCFG="nr5g_disable_mode"
AT+QNWPREFCFG="nr5g_band"
AT+QNWLOCK="common/5g",1,649632,30,78
AT+QNWLOCK="common/5g"
AT+QENG="servingcell"
AT+C5GREG?
```

Confirm that the band selection includes 78, the lock is `PCI=1 / SSB=649632 / SCS=30 / n78`, the serving cell is `NR5G-SA / 001,01 / TAC 7`, and registration reports `+C5GREG: 0,1`. `NOCONN` can indicate an idle, camped state; use C5GREG to confirm registration. **Use SSB ARFCN 649632 for the lock, not carrier ARFCN 650000.**

On this tested firmware, disable mode `2` disables NSA. Exit minicom with `Ctrl+A`, then `X`. Close duplicate AT sessions and leave ModemManager running.

Use the optional reboot command `AT+CFUN=1,1` only when needed. After USB enumeration, run `select_rm500q` again, rediscover the AT port, and reapply or check the preferences and cell lock above. **Do not reboot the modem after the final cell lock.**

## 3 Check registration and connect IPv4

Show only the registration fields:

```bash
mmcli -m "$MODEM" -K | awk -F ':' '
  /modem\.generic\.(state|access-technologies)(\.value\[[0-9]+\])?[[:space:]]*:/ ||
  /modem\.3gpp\.(operator-code|registration-state|packet-service-state)[[:space:]]*:/ {print}
'
```

Require `5gnr / 00101 / home / attached`, with state registered or connected. Then create an **IPv4** PDU session for `internet`:

```bash
sudo mmcli -m "$MODEM" --simple-connect="apn=internet,ip-type=ipv4"
```

If this fails, stop and see the [PDU/IPv4v6 troubleshooting record](troubleshooting/2026-10-05_sa_bringup.md#10-pdu-root-cause-ipv4v6-rejected-by-ocudu). The subscriber on Spark must use session type `1`. The EPS initial bearer may still display IPv4v6; that display does not need changing.

## 4 Find the active bearer and configure dedicated WWAN

Find the **connected + internet** bearer belonging to the selected modem, rather than using the EPS initial bearer. Stop if several bearers match; do not guess an ID:

```bash
BEARER=""
matches=()
discovery_ok=yes
if MODEM_KV=$(mmcli -m "$MODEM" -K); then
  for path in $(printf '%s\n' "$MODEM_KV" |
    grep -oE '/org/freedesktop/ModemManager1/Bearer/[0-9]+' | sort -u); do
    details=$(mmcli -b "$path" -K) || { discovery_ok=no; break; }
    connected=$(printf '%s\n' "$details" | mm_value bearer.status.connected)
    apn=$(printf '%s\n' "$details" | mm_value bearer.properties.apn)
    if [ "$connected" = yes ] && [ "$apn" = internet ]; then matches+=("$path"); fi
  done
else
  discovery_ok=no
fi
if [ "$discovery_ok" = yes ] && [ ${#matches[@]} -eq 1 ] && \
   BEARER_KV=$(mmcli -b "${matches[0]}" -K); then
  BEARER=${matches[0]}
  UE_CONNECTED=$(printf '%s\n' "$BEARER_KV" | mm_value bearer.status.connected)
  UE_SUSPENDED=$(printf '%s\n' "$BEARER_KV" | mm_value bearer.status.suspended)
  WWAN_IF=$(printf '%s\n' "$BEARER_KV" | mm_value bearer.status.interface)
  UE_IP=$(printf '%s\n' "$BEARER_KV" | mm_value bearer.ipv4-config.address)
  UE_PREFIX=$(printf '%s\n' "$BEARER_KV" | mm_value bearer.ipv4-config.prefix)
  UE_MTU=$(printf '%s\n' "$BEARER_KV" | mm_value bearer.ipv4-config.mtu)
  UE_METHOD=$(printf '%s\n' "$BEARER_KV" | mm_value bearer.ipv4-config.method)
  printf 'bearer=%s connected=%s suspended=%s if=%s IPv4=%s/%s mtu=%s method=%s\n' \
    "$BEARER" "$UE_CONNECTED" "$UE_SUSPENDED" "$WWAN_IF" "$UE_IP" "$UE_PREFIX" "$UE_MTU" "$UE_METHOD"
else
  printf 'No unique usable bearer found (discovery=%s, matches=%s); stop and inspect.\n' "$discovery_ok" "${#matches[@]}"
fi
```

Require `connected=yes / suspended=no / static`. Historical values were `wwan0 / 10.45.0.2/30 / MTU 1400`; configure the interface using the values just read. ModemManager supplies the IP parameters, but the host interface still needs configuration.

These commands apply only to a dedicated QMI WWAN interface. If a NetworkManager cellular profile is active, first resolve which component owns WWAN configuration; retain management Wi-Fi and NetworkManager. The script refuses changes when the interface carries a default route, the SSH management path, or another IPv4 address.

```bash
configure_wwan() {
  local current_ips mgmt_dev driver
  [[ -n "$BEARER" && "$UE_CONNECTED" = yes && "$UE_SUSPENDED" = no && "$UE_METHOD" = static ]] || return 1
  [[ "$UE_IP" =~ ^10\.45\.([0-9]{1,3})\.([0-9]{1,3})$ ]] || return 1
  (( 10#${BASH_REMATCH[1]} <= 255 && 10#${BASH_REMATCH[2]} <= 255 )) || return 1
  [[ "$UE_IP" != 10.45.0.1 && "$UE_PREFIX" =~ ^[0-9]+$ && "$UE_MTU" =~ ^[0-9]+$ ]] || return 1
  (( UE_PREFIX >= 1 && UE_PREFIX <= 32 && UE_MTU >= 576 && UE_MTU <= 65535 )) || return 1
  [[ "$WWAN_IF" =~ ^(wwan|wwp|rmnet)[[:alnum:]_.-]*$ ]] || return 1
  ip link show dev "$WWAN_IF" >/dev/null || return 1
  driver=$(basename "$(readlink -f "/sys/class/net/$WWAN_IF/device/driver")")
  [[ "$driver" = qmi_wwan ]] || return 1
  printf '%s\n' "$MODEM_KV" | grep -Fq "$WWAN_IF (net)" || return 1
  [[ -z "$(ip -4 route show default dev "$WWAN_IF")" &&
     -z "$(ip -6 route show default dev "$WWAN_IF")" ]] || return 1
  if [ -n "$SSH_CONNECTION" ]; then
    mgmt_dev=$(ip route get "${SSH_CONNECTION%% *}" | awk '{for(i=1;i<NF;i++) if($i=="dev") print $(i+1)}')
    [[ -n "$mgmt_dev" && "$mgmt_dev" != "$WWAN_IF" ]] || return 1
  fi
  current_ips=$(ip -o -4 addr show dev "$WWAN_IF" | awk '{print $4}')
  [[ -z "$current_ips" || "$current_ips" = "$UE_IP/$UE_PREFIX" ]] || return 1
  sudo ip link set dev "$WWAN_IF" mtu "$UE_MTU" up &&
  sudo ip -4 addr replace "$UE_IP/$UE_PREFIX" dev "$WWAN_IF" noprefixroute &&
  sudo ip -4 route replace 10.45.0.1/32 dev "$WWAN_IF" src "$UE_IP"
}
if configure_wwan; then
  ip -4 route get 10.45.0.1 from "$UE_IP"
  ip -4 route show default
else
  printf 'WWAN configuration is incomplete; check the bearer, dedicated interface, and management routes before testing.\n'
fi
```

The route to `10.45.0.1` must use `$WWAN_IF` and `$UE_IP`, with the Wi-Fi default route retained. This adds only a private host route; it does not add a default route, NAT, or MASQUERADE.

## 5 Check ping, traffic, and KPM

Show the ping summary while keeping errors visible:

```bash
ping -q -I "$WWAN_IF" -c 5 10.45.0.1
```

Require `0% packet loss`. Give Spark the actual `UE_IP` from the bearer summary for a reverse ping. Start `iperf3 -s -B 10.45.0.1` on Spark first.

Run these **one at a time**, keeping the final sender/receiver summaries:

```bash
# Uplink: UE -> Spark, 30 seconds (not yet verified)
iperf3 -c 10.45.0.1 -B "${UE_IP:?Use the terminal where WWAN was configured}" -t 30 -i 0

# Downlink: Spark -> UE, 30 seconds (not yet verified)
iperf3 -c 10.45.0.1 -B "${UE_IP:?Use the terminal where WWAN was configured}" -R -t 30 -i 0
```

The repaired KPM library and monitor are installed and verified to match the new build. Keep **CU, DU, and FlexRIC** running on Spark; the existing iperf server can stay running. Start the 30-second uplink above in the UE terminal where WWAN was configured, then **immediately run the newly installed `xapp_oran_moni` for about 10 seconds on Spark**. See [Spark verification after installation](spark_quick_commands.md#7a-verify-reports-after-the-timestamp-repair). Require `report_age_us`, a UE ID, KPM values, and a normal exit.

The earlier 2026-10-07 run received measurements for UE ID `14`. After the repair, UE ID `2` and the later UE ID `7` each produced **12 reports**, with `report_age_us` ranges of **499–717 µs** and **591–796 µs**, respectively. Subscription, deletion, and xApp exit all succeeded. An intermediate collection window subscribed successfully but received no UE reports. If the result is `NO_UE`, first confirm registration and that UE traffic covers the collection window. `report_age_us` is report age, not application RTT. Fresh ping results, 30-second application uplink/downlink throughput, reporting-period accuracy, and sustained stability remain unverified. Record direction, summary rates, retransmissions, and growth in Spark RF errors on both sides.

For SIM or registration problems, use the actual `(qmi)` port with `qmicli -p`; see [Runbook stage 10](full_reproduction_runbook.md#10-prepare-a-new-ubuntulinux-laptop). Use proxy mode alongside ModemManager rather than competing for a direct QMI session.

## 6 Shut down the UE

Stop traffic with `Ctrl+C` first. Use the variables saved in the same terminal and verify the same modem, then **disconnect the PDU session before removing only this run's manual IP address and host route**. Do not reselect the first modem or flush the entire interface.

```bash
cleanup_ue() {
  local current_identity current_modem_kv driver
  [[ -n "$MODEM" && -n "$MODEM_IDENTITY" && -n "$WWAN_IF" && -n "$UE_IP" && -n "$UE_PREFIX" ]] || return 1
  current_modem_kv=$(mmcli -m "$MODEM" -K) || return 1
  current_identity=$(printf '%s\n' "$current_modem_kv" | mm_value modem.generic.device-identifier)
  [[ "$current_identity" = "$MODEM_IDENTITY" ]] || return 1
  printf '%s\n' "$current_modem_kv" | grep -Fq "$WWAN_IF (net)" || return 1
  sudo mmcli -m "$MODEM" --simple-disconnect || return 1
  driver=$(basename "$(readlink -f "/sys/class/net/$WWAN_IF/device/driver")")
  [[ "$driver" = qmi_wwan ]] || return 1
  if ip -4 route show exact 10.45.0.1/32 dev "$WWAN_IF" | grep -Fq "src $UE_IP"; then
    sudo ip -4 route del 10.45.0.1/32 dev "$WWAN_IF" src "$UE_IP" || return 1
  fi
  if ip -o -4 addr show dev "$WWAN_IF" | awk '{print $4}' | grep -Fxq "$UE_IP/$UE_PREFIX"; then
    sudo ip -4 addr del "$UE_IP/$UE_PREFIX" dev "$WWAN_IF" || return 1
  fi
  printf 'PDU disconnected; the manual address and route for this run are removed.\n'
  ip -br -4 addr show dev "$WWAN_IF"
}
cleanup_ue || printf 'Cleanup is incomplete; check the same modem and interface and any command errors. Do not guess an interface or flush it.\n'
```

If variables were lost or the modem rebooted, reconfirm the original device, interface, and this run's address before cleanup; do not run commands with stale variables. After disconnection and cleanup, optionally run `sudo mmcli -m "$MODEM" --disable`, then unplug the RMU. Shut Spark down using [Spark Quick Commands](spark_quick_commands.md).
