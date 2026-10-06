# Full Reproduction Runbook: Single-Link 5G SA

Last updated: 2026-10-05. Evidence: the operator's successful 2026-10-05 session and the inspected local OCUDU/Open5GS configuration. This manual starts an already installed Spark testbed and configures a new Ubuntu laptop. Software installation, drivers and the OCUDU build environment are documented in [Installation](software_installation_and_drivers.md).

**Startup:** physical connections → USB 3 verification → host performance → clean SA service selection → MongoDB → subscriber/IPv4/NRF checks → required 5GC → `ogstun` → CU → DU → laptop setup → NR/SA cell lock → registration → IPv4 PDU session → active bearer → WWAN configuration → ping → application traffic.

**Shutdown:** applications → PDU session → manual laptop addresses/routes → optional modem disable → DU → CU → required 5GC → optional/legacy services for a full shutdown → optional WebUI/MongoDB stop.

Execute one numbered stage at a time. Check the stated evidence before advancing. Labels identify the execution host: `[SPARK]`, `[LAPTOP]`, or `[RM500Q AT]`. CU/DU and application commands remain in foreground terminals. Keep the laptop's management Wi-Fi default route. Public Internet, NAT, and MASQUERADE are not needed for laptop-to-Spark traffic.

## 1. Baseline and physical connections

The verified radio baseline is n78, carrier ARFCN 650000 (3750 MHz), SSB ARFCN 649632 (3744.48 MHz), 20 MHz, 30 kHz SCS, PCI 1, PLMN `00101`, TAC 7, SST 1, DNN `internet`, and IPv4 only. B210 serial is `3271233`; TX gain is **80**, RX gain **40**, and sample rate **23.04 MS/s**. These gain settings are not calibrated dBm or EIRP. This baseline supersedes the early gain 10/20 trials.

`[SPARK]` Connect B210 through USB 3; do not place the working DU on a 480M USB 2 path. Attach the verified RF antenna path before enabling transmission. Use the authorized laboratory frequency and power conditions. B210 frequency conversion for FR3 is future work.

`[LAPTOP]` Connect RMU500EK with RM500Q-GL, the test USIM, antennas, and the tested data/auxiliary-power USB connections. USB IDs and port numbers must be rediscovered after reboot or movement to a new laptop.

## 2. Check for old RAN processes before probing the B210

`[SPARK]`

```bash
pgrep -a -x odu || true
pgrep -a -x ocu || true
```

If an old experiment is running, follow [shutdown](#16-shutdown) first. For an orphaned foreground terminal, inspect these named processes and then request graceful termination:

`[SPARK]`

```bash
sudo pkill -INT -x odu
sudo pkill -INT -x ocu
pgrep -a -x odu || true
pgrep -a -x ocu || true
```

`pkill` may return status 1 if no matching process exists. Wait until neither CU nor DU remains; do not probe an active DU's radio. These names refer to the single-link experiment on the dedicated host.

`[SPARK]` Once the DU is stopped:

```bash
lsusb
lsusb -t
sudo uhd_find_devices
```

Expect B210 `3271233` at `5000M`. Optional hardware probe, while DU is stopped:

`[SPARK]`

```bash
sudo uhd_usrp_probe --args="type=b200,serial=3271233"
```

Expect `Operating over USB 3` and register loopback success. Do not run device discovery, probe, or benchmark during DU operation.

## 3. Apply the recorded host performance settings

`[SPARK]`

```bash
cd /home/nyu/ocudu
sudo ./scripts/ocudu_performance
```

For the three prompts, answer `Y`, `Y`, `Y`, as in the successful session. The script sets CPU governors to performance, disables DRM KMS polling, and sets four network-buffer limits/defaults to 33554432 bytes. The script describes the network-buffer adjustment as intended for Ethernet USRPs; its individual contribution to this USB B210 result was not isolated. The combined host tuning and privileged DU launch reduced recurring RF failures.

`[SPARK]`

```bash
cat /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor | sort | uniq -c
sudo cat /sys/module/drm_kms_helper/parameters/poll
sysctl net.core.rmem_max net.core.wmem_max net.core.rmem_default net.core.wmem_default
```

Recorded target: `20 performance`, DRM polling `N`, network values `33554432`. Check again after a host reboot; persistence has not been established. Do not add kernel, boot, CPU-isolation, IRQ, or PREEMPT_RT changes.

## 4. Select the local SA services

`[SPARK]` On this dedicated testbed, stop unrelated installed services that were found still running after earlier SA shutdowns:

```bash
sudo systemctl stop open5gs-mmed open5gs-hssd open5gs-pcrfd open5gs-sgwcd open5gs-sgwud
sudo systemctl stop open5gs-bsfd open5gs-seppd
```

The first group is legacy LTE EPC. BSF and SEPP are optional for this single-PLMN experiment; their cleanup does not imply that they caused the SA failures. An absent unit on another deployment should be omitted. WebUI is subscriber administration only and may remain running.

## 5. Start MongoDB and verify the subscriber

`[SPARK]`

```bash
sudo docker ps -a --filter name=open5gs-mongo
sudo docker start open5gs-mongo
sudo docker exec open5gs-mongo mongosh --quiet --eval 'db.adminCommand({ping: 1})'
```

Expect the existing `mongo:7.0-jammy` container and `{ ok: 1 }`. Preserve `open5gs-mongo-data`; do not create a replacement database or delete volumes. Native MongoDB 8 remains disabled.

`[SPARK]` Print only nonsecret subscriber fields:

```bash
sudo docker exec open5gs-mongo mongosh --quiet --eval '
db.getSiblingDB("open5gs").subscribers.find(
  {imsi:"001010000000001"},
  {_id:0,imsi:1,"slice.sst":1,"slice.session.name":1,"slice.session.type":1}
).pretty()
'
```

Required: IMSI `001010000000001`, SST 1, session name `internet`, session **type 1 (IPv4)**. Type 3 is IPv4v6 and was rejected by this OCUDU build. If the subscriber is absent, stop and provision it using the SIM's known authentication parameters through the existing administrative workflow. IMSI cannot reveal Ki/OPc. Never commit credentials, authentication vectors, or derived RAN security keys.

If the existing SST 1 / `internet` session is type 3, this targeted repair updates its type without replacing security fields or other sessions:

`[SPARK]`

```bash
sudo docker exec open5gs-mongo mongosh --quiet --eval '
db.getSiblingDB("open5gs").subscribers.updateOne(
  {imsi:"001010000000001"},
  {$set:{"slice.$[s].session.$[p].type":1}},
  {arrayFilters:[{"s.sst":1},{"p.name":"internet"}]}
)
'
```

Re-run the projection and require type 1. No matched slice/session means the intended entry has not been repaired. The session recorded on 2026-10-05 occupied `slice.0.session.0`; named array filters avoid assuming that index on a later database.

## 6. Check infrastructure configuration

`[SPARK]`

```bash
grep -n -A5 'serving:' /etc/open5gs/nrf.yaml
grep -n -A15 -E 'guami:|tai:|plmn_support:' /etc/open5gs/amf.yaml
grep -nE 'device_args|srate|clock:|clock_ppm|freq_offset|tx_gain|rx_gain|dl_arfcn|band:|channel_bandwidth_MHz|common_scs|plmn:|tac:|pci:|all_level' /home/nyu/ocudu/configs/du1_b210_n78_20mhz.yml
sed -n '1,85p' /home/nyu/ocudu/configs/cu.yml
```

NRF `serving` and AMF GUAMI/TAI/PLMN support must be MCC `001`, MNC `01`; AMF TAC 7 and SST 1. NRF's old `999/70` entry caused NF discovery failure and Registration Reject 95. Compare the OCUDU output with [configuration records](project_progress_and_configuration.md#configuration-records). Preserve leading zeros when editing PLMN fields.

Runtime IP relationships: CU N2 `127.0.0.1` → AMF `127.0.0.5:38412`; DU F1-C `127.0.10.2` → CU `127.0.10.1:38472`; DU F1-U `127.0.10.2:2152` ↔ CU `127.0.10.1:2152`; CU N3 `127.0.0.1:2152` ↔ UPF `127.0.0.7:2152`; SMF PFCP `127.0.0.4:8805` ↔ UPF `127.0.0.7:8805`.

## 7. Start only the required 5GC functions

`[SPARK]` Execute sequentially, checking for command errors:

```bash
sudo systemctl daemon-reload
sudo systemctl start open5gs-nrfd
sudo systemctl start open5gs-scpd
sudo systemctl start open5gs-udrd
sudo systemctl start open5gs-udmd
sudo systemctl start open5gs-ausfd
sudo systemctl start open5gs-pcfd
sudo systemctl start open5gs-nssfd
sudo systemctl start open5gs-upfd
sudo systemctl start open5gs-smfd
sudo systemctl start open5gs-amfd
```

`[SPARK]`

```bash
for service in open5gs-nrfd open5gs-scpd open5gs-udrd open5gs-udmd open5gs-ausfd open5gs-pcfd open5gs-nssfd open5gs-upfd open5gs-smfd open5gs-amfd; do
  printf '%-20s ' "$service"
  systemctl is-active "$service"
done
sudo ss -lnpA sctp
sudo ss -lunp | grep -E ':8805|:2152'
sudo journalctl -u open5gs-amfd -u open5gs-smfd -u open5gs-upfd --since '5 minutes ago' --no-pager
```

Every listed service must be `active`; AMF listens on `127.0.0.5:38412`, UPF on `127.0.0.7:2152`, and SMF/UPF PFCP association succeeds. Review current timestamps; old reject or shutdown records do not describe this startup.

`[SPARK]`

```bash
ip link show ogstun
ip -4 addr show dev ogstun
ip -4 route show
systemctl is-active systemd-networkd
cat /etc/systemd/network/99-open5gs.netdev /etc/systemd/network/99-open5gs.network
```

Require the existing `ogstun` to be administratively UP with MTU 1400, gateway `10.45.0.1/16`, and a usable route to `10.45.0.0/16`. The inspected host has persistent `99-open5gs.netdev` (`Name=ogstun`, `Kind=tun`) and `99-open5gs.network` (address, route and MTU); the UPF service requests `systemd-networkd`. This provides a configured recovery mechanism after reboot, but a cold-reboot result remains untested. Missing interface/address/route requires diagnosis of these files and networkd; do not blindly create another TUN or restart the host's management networking. A TUN state display alone is insufficient to judge data readiness. Do not add Internet NAT.

## 8. Archive old RAN logs and start the CU

`[SPARK]` Only while CU/DU are stopped, preserve previous logs instead of deleting them. These archives remain local and may contain security material:

```bash
RAN_ARCHIVE=$(mktemp -d /tmp/falcomm-ran-logs.XXXXXX)
sudo chmod 700 "$RAN_ARCHIVE"
for logfile in /tmp/cu.log /tmp/du.log; do
  if sudo test -f "$logfile"; then
    sudo mv "$logfile" "$RAN_ARCHIVE/"
  fi
done
```

`[SPARK]` In the CU terminal:

```bash
cd /home/nyu/ocudu
sudo build/apps/cu/ocu -c configs/cu.yml
```

Expect N2 connection to `127.0.0.5:38412`, F1-C listener at `127.0.10.1:38472`, and `==== CU started ===`. Both RAN logs use `all_level: info` for local debugging; sanitize excerpts before sharing them.

`[SPARK]` In a separate diagnostics terminal:

```bash
sudo journalctl -u open5gs-amfd --since '5 minutes ago' --no-pager | grep -Ei 'gNB|NGAP|error|reject'
sudo ss -anpA sctp
```

Require the current AMF `gNB-N2 accepted` / added-gNB entry and an established N2 association.

## 9. Confirm RF and start the DU

Confirm n78, ARFCN 650000, 3750 MHz, 20 MHz, 30 kHz, PLMN `00101`, TAC 7, PCI 1, B210 `3271233`, TX gain 80 and RX gain 40 against stage 6. The following command enables RF transmission. Do not silently change the baseline gain or frequency.

`[SPARK]` In the DU terminal:

```bash
cd /home/nyu/ocudu
sudo build/apps/du_split_8/odu -c configs/du1_b210_n78_20mhz.yml
```

Expect B210 / USB 3 initialization, 23.04 MHz master clock, cell PCI 1 / 20 MHz / n78 / carrier ARFCN 650000 / SSB ARFCN 649632, F1-C connection completed, and `==== DU started ===`. Check the running DU without opening the B210 from another tool:

`[SPARK]` In a separate diagnostics terminal:

```bash
sudo ss -anpA sctp
sudo ss -lunp | grep 2152
sudo grep -Ei 'PRACH|PUSCH|CRC|SINR|UE Create|underflow|overflow|late' /tmp/du.log | tail -100
sudo grep -Ei 'PDUSession|Unsupported PDU Session Type|Validation.*failed' /tmp/cu.log | tail -80
sudo grep -Eic 'underflow' /tmp/du.log
sudo grep -Eic 'late' /tmp/du.log
sudo grep -Eic 'overflow' /tmp/du.log
```

Both N2 and F1-C must be `ESTAB`. GTP-U listeners include DU `127.0.10.2`, CU F1-U `127.0.10.1`, CU N3 `127.0.0.1`, UPF `127.0.0.7`, all port 2152. Open5GS SMF may also listen on `127.0.0.4:2152`; it is separate from the four data-path endpoints.

Counters of zero make `grep -c` return status 1; this is not itself a failure. Observe whether counters grow during traffic. For an ongoing monitor:

`[SPARK]` In a separate diagnostics terminal:

```bash
sudo tail -F /tmp/du.log | grep --line-buffered -Ei 'underflow|overflow|late|RF.*failure'
```

Stop this monitor with `Ctrl+C`. Continuous RF failures require investigation; an isolated late event does not establish a stable long-duration result. The recorded post-tuning check was underflow 0, late 1, not a guaranteed result for every run.

## 10. Prepare a new Ubuntu/Linux laptop

`[LAPTOP]` Install only if missing on this laptop:

```bash
sudo apt update
sudo apt install -y modemmanager libqmi-utils minicom iproute2 iperf3 usbutils
sudo systemctl enable --now ModemManager
systemctl is-active ModemManager
lsusb
ls -l /dev/cdc-wdm* /dev/ttyUSB* 2>/dev/null || true
ip -br link | grep -E 'wwan|rmnet' || true
mmcli -L
```

Expect USB `2c7c:0800`, a Quectel RM500QGL_VH modem, QMI, AT and network ports. Drivers observed in the working setup were `option`, `qmi_wwan`, and `cdc_wdm`. If ports are absent, follow the [laptop driver inspection](software_installation_and_drivers.md#72-kernel-driver-responsibilities) and inspect recurring disconnects before changing modem firmware.

`[LAPTOP]` For this single-modem setup, discover the ID and verify the model:

```bash
MODEM=$(mmcli -L | grep -oP 'Modem/\K[0-9]+' | head -1)
if [ -n "$MODEM" ]; then
  mmcli -m "$MODEM"
else
  printf 'No ModemManager modem found; stop here.\n'
fi
```

If several modems exist, select the RM500Q path shown by `mmcli -L`; do not automatically configure a different modem. A previously disabled modem needs `sudo mmcli -m "$MODEM" --enable`. Firmware verified here is `RM500QGLABR13A03M4G`.

`[LAPTOP]` Find a port marked `(qmi)` and use QMI proxy with ModemManager:

```bash
QMI_PORT=$(mmcli -m "$MODEM" | grep -oE 'cdc-wdm[0-9]+ \(qmi\)' | head -1 | awk '{print $1}')
if [ -n "$QMI_PORT" ]; then
  sudo qmicli -p -d "/dev/$QMI_PORT" --uim-get-card-status
  sudo qmicli -p -d "/dev/$QMI_PORT" --nas-get-serving-system
  sudo qmicli -p -d "/dev/$QMI_PORT" --nas-get-signal-info
else
  printf 'No QMI port listed; inspect mmcli ports and stop here.\n'
fi
```

SIM must be present and USIM ready. An all-FF ICCID was observed; by itself it did not prevent this test SIM from authenticating. Do not use a fixed SIM index such as `mmcli -i 0` on a new laptop.

## 11. Configure RM500Q NR SA and lock the private cell

`[LAPTOP]` Inspect `mmcli -m "$MODEM"` ports. Choose an actual `(at)` port; the working laptop used ttyUSB2 and ttyUSB3, but numbering changes:

```bash
AT_PORT=$(mmcli -m "$MODEM" | grep -oE 'ttyUSB[0-9]+ \(at\)' | head -1 | awk '{print $1}')
if [ -n "$AT_PORT" ]; then
  sudo minicom -D "/dev/$AT_PORT" -b 115200
else
  printf 'No AT port listed; inspect RM500Q enumeration and stop here.\n'
fi
```

`[RM500Q AT]` Type into minicom, not the Linux shell:

```text
AT
AT+CIMI
AT+CFUN=1,1
```

Expect `OK`, test IMSI `001010000000001`, then a modem reboot. USB ports and ModemManager IDs may change. Exit/reopen minicom after enumeration, discover the modem and AT port again using stages 10–11, and wait for `RDY` / `+CPIN: READY` where provided. Do not reboot again after the final cell lock.

`[RM500Q AT]` Set preferences in this order, requiring `OK` after each write:

```text
AT+QNWPREFCFG="mode_pref",NR5G
AT+QNWPREFCFG="mode_pref"
AT+QNWPREFCFG="nr5g_disable_mode",2
AT+QNWPREFCFG="nr5g_disable_mode"
AT+QNWPREFCFG="nr5g_band"
AT+QNWLOCK="common/5g",1,649632,30,78
AT+QNWLOCK="common/5g"
```

For this tested firmware, disable mode 2 disables NSA and gives SA-only operation. The band query must include n78; the tested selection was 78. The lock parameters are **PCI 1, SSB ARFCN 649632, SCS 30, band 78**. Do not use carrier ARFCN 650000 in place of the SSB lock value. Old `QCFG` preference commands returned ERROR on this firmware. Recheck preferences and lock after every physical/CFUN reboot because they may reset.

`[RM500Q AT]`

```text
AT+QENG="servingcell"
AT+C5GREG?
AT+QNWINFO
```

Require `NR5G-SA`, PLMN `001/01`, PCI 1, TAC 7, SSB 649632, band 78 and `+C5GREG: 0,1`. `NOCONN` in QENG is acceptable idle/camped state; confirm registration with C5GREG. If not registered, use `AT+QSCAN=3` to establish whether the private cell is detectable, then follow [the troubleshooting record](troubleshooting/2026-10-05_sa_bringup.md). A scan can take time; retain its result.

Exit minicom with `Ctrl+A`, then `X`. Leave ModemManager managing the connection; do not open the same AT port in another terminal simultaneously. If another process holds the selected AT port, close the conflicting session and inspect ownership rather than resetting the firmware.

## 12. Register and create an IPv4 PDU session

`[LAPTOP]` Rediscover the modem after reboot:

```bash
MODEM=$(mmcli -L | grep -oP 'Modem/\K[0-9]+' | head -1)
mmcli -m "$MODEM"
```

Require registered state, access technology `5gnr`, operator `00101`, home registration, and packet service attached. Then:

`[LAPTOP]`

```bash
sudo mmcli -m "$MODEM" --simple-connect="apn=internet,ip-type=ipv4"
mmcli -m "$MODEM"
```

Expect `successfully connected the modem` and connected state. If it fails, diagnose PDU setup separately from registration. CU logs may show `Unsupported PDU Session Type: ipv4v6` even though Open5GS allocated an IP; check subscriber type 1 first.

## 13. Inspect the active bearer and configure WWAN

`[LAPTOP]` Obtain all bearer paths from the selected modem and find its connected `internet` bearer. Define this helper once in the current laptop shell:

```bash
mm_value() {
  awk -F ':' -v key="$1" '
    { field=$1; gsub(/^[ \t]+|[ \t]+$/, "", field) }
    field==key { sub(/^[^:]*:[ \t]*/, ""); sub(/[ \t]+$/, ""); print; exit }
  '
}

BEARER=""
for path in $(mmcli -m "$MODEM" -K | grep -oE '/org/freedesktop/ModemManager1/Bearer/[0-9]+' | sort -u); do
  details=$(mmcli -b "$path" -K)
  connected=$(printf '%s\n' "$details" | mm_value bearer.status.connected)
  apn=$(printf '%s\n' "$details" | mm_value bearer.properties.apn)
  if [ "$connected" = yes ] && [ "$apn" = internet ]; then
    BEARER="$path"
    break
  fi
done
if [ -n "$BEARER" ]; then
  mmcli -b "$BEARER"
else
  printf 'No connected internet bearer found; stop here.\n'
fi
```

Do not use `--list-bearers`; the tested CLI rejected that form. `mmcli -m` → bearer path → `mmcli -b` is supported. The modem's **EPS initial bearer ip type** may still say `ipv4v6`; the **active bearer** must say IPv4. Expect connected yes, suspended no, an interface (historically `wwan0`), and static IPv4 settings.

`[LAPTOP]` Extract the current bearer allocation instead of hard-coding `10.45.0.2` or `wwan0`:

```bash
BEARER_KV=$(mmcli -b "$BEARER" -K)
WWAN_IF=$(printf '%s\n' "$BEARER_KV" | mm_value bearer.status.interface)
UE_IP=$(printf '%s\n' "$BEARER_KV" | mm_value bearer.ipv4-config.address)
UE_PREFIX=$(printf '%s\n' "$BEARER_KV" | mm_value bearer.ipv4-config.prefix)
UE_MTU=$(printf '%s\n' "$BEARER_KV" | mm_value bearer.ipv4-config.mtu)
UE_METHOD=$(printf '%s\n' "$BEARER_KV" | mm_value bearer.ipv4-config.method)
printf 'interface=%s address=%s/%s mtu=%s method=%s\n' "$WWAN_IF" "$UE_IP" "$UE_PREFIX" "$UE_MTU" "$UE_METHOD"
```

The successful session reported `wwan0`, `10.45.0.2/30`, gateway `10.45.0.1`, MTU 1400, method static. Current values may differ. Stop if fields are empty/`--`, the interface is absent, the address is outside the private allocation, or the method is not static. Use the displayed bearer values; do not substitute management-interface addresses.

ModemManager reports the connection's IP settings; it does not itself configure the host interface. This explains the observed connected bearer with a DOWN/addressless WWAN device. See [ModemManager IP setup](https://modemmanager.org/docs/modemmanager/ip-connectivity-setup-in-lte-modems/).

`[LAPTOP]` On the dedicated modem interface only, replace stale manual IPv4 settings and add a host route to Spark. This narrow route also supports allocations whose reported prefix does not include `10.45.0.1`:

```bash
if [ "$UE_METHOD" = static ] && [[ "$UE_IP" = 10.45.* ]] && \
   [[ "$UE_PREFIX" =~ ^[0-9]+$ ]] && (( UE_PREFIX >= 1 && UE_PREFIX <= 32 )) && \
   [[ "$UE_MTU" =~ ^[0-9]+$ ]] && (( UE_MTU >= 576 && UE_MTU <= 65535 )) && \
   [ -n "$WWAN_IF" ] && ip link show dev "$WWAN_IF" >/dev/null 2>&1; then
  sudo ip -4 addr flush dev "$WWAN_IF" &&
  sudo ip link set dev "$WWAN_IF" mtu "$UE_MTU" &&
  sudo ip link set dev "$WWAN_IF" up &&
  sudo ip -4 addr add "$UE_IP/$UE_PREFIX" dev "$WWAN_IF" &&
  sudo ip -4 route replace 10.45.0.1/32 dev "$WWAN_IF" src "$UE_IP"
else
  printf 'Invalid static bearer configuration; stop here.\n'
fi
ip -4 addr show dev "$WWAN_IF"
ip -4 route get 10.45.0.1 from "$UE_IP"
ip -4 route show default
```

Verify destination `10.45.0.1` uses the modem interface and `$UE_IP`; management Wi-Fi default remains unchanged. If NetworkManager manages an active cellular profile, use one owner for WWAN configuration: disconnect that profile before manual setup, or configure equivalent local routes in the profile. Do not disable NetworkManager globally.

## 14. Ping and throughput

`[LAPTOP]`

```bash
ping -I "$WWAN_IF" -c 5 10.45.0.1
```

`[SPARK]` Enter the **current** bearer address from the laptop, then reverse ping:

```bash
read -r -p 'Current UE IPv4 address from active bearer: ' UE_IP
ping -I ogstun -c 5 "$UE_IP"
iperf3 -s -B 10.45.0.1
```

Keep the iperf server in its own terminal. The original successful uplink test used a 10-second default run, with sender 6.29 Mbit/s / receiver 5.09 Mbit/s and 0 sender retransmissions. That is a functional result, not optimized throughput.

`[LAPTOP]` Run individually; 30–60-second and reverse/UDP tests remain validation work:

```bash
iperf3 -c 10.45.0.1 -B "$UE_IP" -t 30
iperf3 -c 10.45.0.1 -B "$UE_IP" -R -t 30
iperf3 -c 10.45.0.1 -B "$UE_IP" -u -b 2M -t 30
```

If stable, repeat UDP at 4M, 6M, 8M, and 10M individually. Record direction, duration, sender/receiver rates, retransmissions, UDP jitter/loss, and concurrent DU RF-error counts. Do not infer UDP/video performance from the first TCP test. If routing is correct but traffic fails, check host firewall and service binding before adding forwarding/NAT.

## 15. Video experiment (procedure; not yet verified)

`[SPARK]` Install FFmpeg if missing and receive in a graphical session:

```bash
sudo apt install -y ffmpeg
ffplay -fflags nobuffer 'udp://10.45.0.1:5000?fifo_size=1000000&overrun_nonfatal=1'
```

For a headless receiver, use a local recording instead of ffplay (choose a new output filename):

`[SPARK]`

```bash
ffmpeg -i 'udp://10.45.0.1:5000?fifo_size=1000000&overrun_nonfatal=1' -c copy received-private-5g.ts
```

`[LAPTOP]` Confirm a supported capture device/format before sending:

```bash
sudo apt install -y ffmpeg v4l-utils
v4l2-ctl --list-devices
read -r -p 'Camera device path shown above: ' CAMERA_DEVICE
v4l2-ctl -d "$CAMERA_DEVICE" --list-formats-ext
```

`[LAPTOP]` Adapt capture size/rate to a supported format; the example is 640×480 at 30 frames/s:

```bash
ffmpeg -f v4l2 -framerate 30 -video_size 640x480 -i "$CAMERA_DEVICE" \
  -an -c:v libx264 -pix_fmt yuv420p -preset ultrafast -tune zerolatency \
  -b:v 3M -maxrate 3M -bufsize 1M -g 30 -f mpegts \
  "udp://10.45.0.1:5000?localaddr=$UE_IP&pkt_size=1316"
```

The destination/host route and explicit sender `localaddr` keep traffic on the private link. The 1316-byte UDP payload fits the recorded 1400-byte MTU with IPv4/UDP headers. See [FFmpeg UDP options](https://ffmpeg.org/ffmpeg-protocols.html#udp). Confirm camera permissions, encoder availability and receiver firewall locally; do not claim this video procedure was demonstrated on 2026-10-05.

## 16. Shutdown

Stop iperf, ffmpeg, ffplay, and any receiver with `Ctrl+C` before releasing the bearer.

`[LAPTOP]` In the shell retaining the discovered variables:

```bash
MODEM=$(mmcli -L | grep -oP 'Modem/\K[0-9]+' | head -1)
sudo mmcli -m "$MODEM" --simple-disconnect
mmcli -m "$MODEM"
sudo ip -4 route del 10.45.0.1/32 dev "$WWAN_IF"
sudo ip -4 addr flush dev "$WWAN_IF"
sudo ip link set dev "$WWAN_IF" down
ip addr show dev "$WWAN_IF"
```

If the host route is already absent, its deletion may return `No such process`; continue with address removal. If shell variables were lost, rediscover the modem and its network port before cleanup; do not flush a guessed interface. Confirm the manual test IP is gone. Optional radio disable when the RMU remains connected:

`[LAPTOP]`

```bash
sudo mmcli -m "$MODEM" --disable
```

Disconnect the bearer before physically unplugging the RMU. Next, in the DU terminal press `Ctrl+C` and require `Stopping...`; RF transmission ends. Then press `Ctrl+C` in the CU terminal. For orphaned sessions use the inspected-name `pkill -INT -x` method in stage 2. Avoid SIGKILL as routine cleanup.

`[SPARK]` Stop the core sequentially:

```bash
sudo systemctl stop open5gs-amfd
sudo systemctl stop open5gs-smfd
sudo systemctl stop open5gs-upfd
sudo systemctl stop open5gs-nssfd
sudo systemctl stop open5gs-pcfd
sudo systemctl stop open5gs-ausfd
sudo systemctl stop open5gs-udmd
sudo systemctl stop open5gs-udrd
sudo systemctl stop open5gs-scpd
sudo systemctl stop open5gs-nrfd
```

`[SPARK]` For a **full Open5GS shutdown**, also stop optional/legacy services if installed:

```bash
sudo systemctl stop open5gs-bsfd open5gs-seppd open5gs-mmed open5gs-hssd open5gs-pcrfd open5gs-sgwcd open5gs-sgwud
```

`[SPARK]` WebUI and MongoDB can remain available between experiments; stop them only if desired. Omit WebUI if its unit is absent:

```bash
sudo systemctl stop open5gs-webui
sudo docker stop open5gs-mongo
```

Never remove containers/volumes during normal shutdown. Verify:

`[SPARK]`

```bash
pgrep -a -x odu || true
pgrep -a -x ocu || true
systemctl --no-pager list-units 'open5gs-*' --type=service --state=running
sudo ss -lunp | grep 2152 || true
sudo docker ps --filter name=open5gs-mongo
```

For a full shutdown, no CU/DU, running Open5GS services, or their GTP-U listeners remain. Docker is separate: MongoDB may run intentionally or be stopped; the persistent container/volume must remain.

## 17. Acceptance and next session

- [ ] B210 `3271233` at USB 3; performance settings checked.
- [ ] All ten required core functions active; NRF PLMN correct; subscriber type 1.
- [ ] CU N2 and DU F1-C established; expected four GTP-U endpoints present.
- [ ] RM500Q detects the private SSB and reports NR5G-SA / PLMN `00101` / C5GREG registered.
- [ ] Authentication and packet attachment succeed; IPv4 bearer connected.
- [ ] Actual bearer address/prefix/MTU applied to the laptop modem interface.
- [ ] Private route confirmed; laptop and Spark reach each other; iperf transfers data.
- [ ] RF-error growth and test duration recorded; traffic stopped before teardown.

Cold restart on a new laptop, longer TCP tests, downlink, UDP loss/jitter, live video and long-duration RF stability remain to be measured. Freeze a reproducible Single-Link Baseline v1 before adding another path, MPTCP, FlexRIC telemetry, xApps or AI steering.
