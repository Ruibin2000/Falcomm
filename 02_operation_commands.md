# Operational Command Reference
# Single-B210 / Single-RM500Q 5G SA Bring-Up

## 1. Scope and Operating Rules

This procedure validates the minimum 5G SA chain:

```text
Quectel RM500Q-GL UE → NR air interface → USRP B210 → OCUDU DU
→ OCUDU CU → Open5GS 5GC → PDU session → UE wwan0 address
```

Use this procedure only for the first single-link bring-up. Do not start a second DU or B210, FlexRIC, MPTCP, or any AI control component. Start every component in a separate foreground terminal and inspect its output before moving to the next component.

The DU transmits when it starts. Do not run `uhd_usrp_probe` or `benchmark_rate` while the DU owns B210 #1.

## 2. Recorded Baseline

| Item | Value |
|---|---|
| OCUDU repository | `/home/nyu/ocudu` |
| CU executable | `build/apps/cu/ocu` |
| DU executable | `build/apps/du_split_8/odu` |
| CU configuration | `configs/cu.yml` |
| DU configuration | `configs/du1_b210_n78_20mhz.yml` |
| B210 serial | `3271233` |
| Radio interface | UHD over USB 3 |
| Band / ARFCN | n78 / 650000 |
| DL and UL frequency | 3750 MHz |
| Bandwidth / SCS | 20 MHz / 30 kHz |
| PLMN / TAC / PCI | `00101` / `7` / `1` |
| Initial TX / RX gain | `10` / `40` |
| CU N2 | `127.0.0.1` → AMF `127.0.0.5:38412` |
| CU/DU F1 | CU `127.0.10.1:38472`; DU `127.0.10.2` |
| Open5GS UE subnet | `10.45.0.0/16`; gateway `10.45.0.1` |

`tx_gain` is a UHD gain setting, not a calibrated radiated-power value. Use the B210 and UE antennas at short range and increase gain only after verifying that the private cell is not detectable.

## 3. Preflight: Verify B210 While the DU Is Stopped

```bash
uhd_find_devices

uhd_usrp_probe --args="type=b200,serial=3271233"

lsusb -t
```

Expected results include:

```text
serial: 3271233
product: B210
Operating over USB 3.
Register loopback test passed
5000M
```

Do not start the DU if this check fails. Do not repeat this check after starting the DU.

## 4. Start MongoDB

```bash
sudo docker start open5gs-mongo

sudo docker exec open5gs-mongo \
  mongosh --quiet --eval 'db.adminCommand({ping: 1})'
```

Expected result:

```text
{ ok: 1 }
```

Inspect the container only when diagnosis is required:

```bash
sudo docker ps -a --filter name=open5gs-mongo
sudo docker logs --tail 50 open5gs-mongo
```

## 5. Start the 5G Core

If `systemctl status` reports that an Open5GS unit file changed on disk, reload the systemd unit definitions once:

```bash
sudo systemctl daemon-reload
```

Start only the 5G Core services used by this SA testbed:

```bash
sudo systemctl start \
  open5gs-nrfd \
  open5gs-scpd \
  open5gs-udrd \
  open5gs-udmd \
  open5gs-ausfd \
  open5gs-nssfd \
  open5gs-pcfd \
  open5gs-bsfd \
  open5gs-smfd \
  open5gs-upfd \
  open5gs-amfd
```

Verify the core before starting the CU:

```bash
systemctl --no-pager --full status \
  open5gs-amfd \
  open5gs-smfd \
  open5gs-upfd

sudo ss -lnpA sctp | grep 38412

journalctl -u open5gs-amfd -n 50 --no-pager
journalctl -u open5gs-smfd -n 50 --no-pager
journalctl -u open5gs-upfd -n 50 --no-pager
```

Required evidence:

```text
open5gs-amfd.service: active (running)
open5gs-smfd.service: active (running)
open5gs-upfd.service: active (running)
127.0.0.5:38412
PFCP associated [127.0.0.7]:8805
```

## 6. Start the OCUDU CU

In a dedicated terminal, run:

```bash
cd /home/nyu/ocudu

build/apps/cu/ocu \
  -c configs/cu.yml
```

Required CU output:

```text
N2: Connection to AMF on 127.0.0.5:38412 completed
F1-C: Listening for new connections on bind addresses 127.0.10.1, port 38472...
==== CU started ===
```

The following scheduling-priority warnings are expected at the present stage and are not blockers:

```text
Scheduling priority ... Not enough privileges
```

Confirm the current N2 association from the AMF:

```bash
journalctl -u open5gs-amfd -n 80 --no-pager | \
  grep -Ei 'gNB-N2 accepted|Number of gNBs'
```

Expected current-session evidence:

```text
gNB-N2 accepted[127.0.0.1]
[Added] Number of gNBs is now 1
```

## 7. Confirm RF Parameters Before Starting the DU

Run this while the DU remains stopped:

```bash
grep -nE 'device_args|srate|tx_gain|rx_gain|dl_arfcn|band:|channel_bandwidth_MHz|common_scs|plmn:|tac:|pci:' \
  /home/nyu/ocudu/configs/du1_b210_n78_20mhz.yml
```

Proceed only when the output matches the recorded baseline, especially:

```text
tx_gain: 10
rx_gain: 40
dl_arfcn: 650000
band: 78
channel_bandwidth_MHz: 20
common_scs: 30
plmn: "00101"
tac: 7
pci: 1
```

Verify that the B210 TX/RX port has its antenna attached, the Quectel antenna is nearby, and the test location and frequency use are authorized. Starting the next command activates RF transmission at approximately 3.75 GHz.

## 8. Start the OCUDU DU

In a second dedicated terminal, run:

```bash
cd /home/nyu/ocudu

build/apps/du_split_8/odu \
  -c configs/du1_b210_n78_20mhz.yml
```

Required DU output:

```text
Detected Device: B210
Operating over USB 3.
Register loopback test passed
Actually got clock rate 23.040000 MHz.

Cell pci=1, bw=20 MHz, 1T1R,
dl_arfcn=650000 (n78),
dl_freq=3750 MHz,
ul_freq=3750 MHz

F1-C: Connection to CU-CP on 127.0.10.1:38472 completed
==== DU started ===
```

Check both control-plane associations:

```bash
sudo ss -anpA sctp
```

Expected associations include:

```text
127.0.0.1     → 127.0.0.5:38412     ESTAB  # CU ↔ AMF N2
127.0.10.2    → 127.0.10.1:38472    ESTAB  # DU ↔ CU F1-C
```

If the DU prints repeated UHD `underflow`, `overflow`, or `late` messages, stop the DU with `Ctrl+C` and retain the output for diagnosis. Do not change kernel, real-time scheduling, or CPU settings as a first response.

## 9. Goal 1: Detect the Private PLMN on the Quectel Laptop

Run the following commands on the Ubuntu laptop attached to the RM500Q-GL. The modem number is discovered dynamically and is never hard-coded.

```bash
MODEM=$(mmcli -L | grep -oP 'Modem/\K[0-9]+' | head -1)

QMI=$(mmcli -m "$MODEM" \
  | grep 'primary port' \
  | awk '{print $NF}')

echo "MODEM=$MODEM"
echo "QMI=/dev/$QMI"

sudo qmicli -p -d "/dev/$QMI" \
  --nas-get-serving-system

sudo qmicli -p -d "/dev/$QMI" \
  --nas-get-signal-info
```

Goal 1 passes when the RM500Q reports the private PLMN `001/01` on 5G NR. Registration is not required for this goal.

If the UE does not show the private PLMN, perform a scan before modifying any core or subscriber setting:

```bash
sudo qmicli -p -d "/dev/$QMI" \
  --nas-network-scan
```

When only a public PLMN is visible, diagnose in this order: DU active state, B210 availability, ARFCN and frequency, band, bandwidth, SCS, antenna/RF path, then minimum necessary TX gain. Open5GS subscriber configuration is not relevant until the private cell is detectable.

## 10. Goals 2 and 3: Registration and PDU Session Preconditions

Do not attempt authentication until the following subscriber values are known and match the Open5GS database entry exactly:

```text
IMSI/SUPI
K
OP or OPc
AMF
MCC/MNC
DNN
```

Once registration is successful, inspect AMF, SMF, and UPF logs for the registration, authentication, and PDU-session sequence. Before PDU-session testing, verify that `ogstun` has the expected IPv4 gateway address:

```bash
ip -4 addr show dev ogstun
ip route show
```

The expected gateway is `10.45.0.1/16`. Do not claim user-plane readiness if this address is absent.

After a successful PDU session, verify the UE interface on the laptop:

```bash
ip addr show wwan0
ip route
```

Use an explicitly configured UPF or server address for the first ping. Do not assume public Internet NAT is available.

## 11. Stop the Testbed

Stop components in the reverse order. This immediately stops RF transmission before the core is shut down.

1. In the DU terminal, press `Ctrl+C`.
2. In the CU terminal, press `Ctrl+C`.
3. Stop the 5G Core:

```bash
sudo systemctl stop \
  open5gs-amfd \
  open5gs-upfd \
  open5gs-smfd \
  open5gs-bsfd \
  open5gs-pcfd \
  open5gs-nssfd \
  open5gs-ausfd \
  open5gs-udmd \
  open5gs-udrd \
  open5gs-scpd \
  open5gs-nrfd
```

4. Stop MongoDB:

```bash
sudo docker stop open5gs-mongo
```

Do not start native MongoDB 8. The deployment uses the `open5gs-mongo` MongoDB 7 Docker container.

## 12. Quick Diagnostic Commands

```bash
ps aux | grep -E '[o]cu|[o]du|open5gs|mongod'

sudo ss -anpA sctp

journalctl -u open5gs-amfd -n 100 --no-pager
journalctl -u open5gs-smfd -n 100 --no-pager
journalctl -u open5gs-upfd -n 100 --no-pager

sudo docker ps -a --filter name=open5gs-mongo

tail -n 100 /tmp/cu.log
tail -n 100 /tmp/du.log
```

Never use the B210 diagnostics below while the DU is active:

```bash
uhd_find_devices
uhd_usrp_probe --args="type=b200,serial=3271233"
/usr/libexec/uhd/examples/benchmark_rate \
  --args="type=b200,serial=3271233" \
  --rx_rate 30.72e6 \
  --duration 20
```
