# 4. 给 Codex 的 Prompt
# Continue DGX Spark + OCUDU + Open5GS + B210 deployment

You are continuing an already working 5G / AI-RAN testbed deployment.

Do NOT restart the project from scratch.
Do NOT replace the NVIDIA kernel.
Do NOT install PREEMPT_RT.
Do NOT change multiple variables at once.
Work incrementally and validate every step from real command output.

---

## Project Goal

Build a dual-link 5G / AI-RAN testbed on one NVIDIA DGX Spark:

```text
                         DGX Spark
          ┌─────────────────────────────────┐
          │                                 │
          │            FlexRIC              │
          │          Near-RT RIC            │
          │           ▲       ▲             │
          │         E2│       │E2           │
          │           │       │             │
          │        OCUDU DU1 OCUDU DU2      │
          │           │       │             │
          │          UHD     UHD            │
          └───────────┼───────┼─────────────┘
                      │       │
                    USB3    USB3
                      │       │
                  B210 #1  B210 #2
                      │       │
                    Cell 1  Cell 2
                       \     /
                        \ F1/
                         \ /
                       OCUDU CU
                          │
                        N2/N3
                          │
                       Open5GS
                          │
                          N6
                          │
                     MPTCP Server
```

Later:

```text
DU1 / DU2
   ↓ E2
FlexRIC
   ↓
xApp
   ↓
AI predictor / policy controller
   ↓
Linux MPTCP path manager / scheduler
```

---

## Current Machine

```text
NVIDIA DGX Spark
Architecture: aarch64
Ubuntu:       24.04.5 LTS
Kernel:       7.0.0-1019-nvidia
PREEMPT:      PREEMPT_DYNAMIC
CPU:          20 cores
RAM:          121 GiB
```

CPU topology:

```text
A725:  CPU 0-4, 10-14
X925:  CPU 5-9, 15-19
```

No CPU isolation / IRQ tuning / nohz_full / rcu_nocbs has been applied yet.

This is intentional.

---

## MPTCP

Already supported by the existing NVIDIA kernel:

```text
CONFIG_MPTCP=y
CONFIG_MPTCP_IPV6=y
net.mptcp.enabled = 1
```

Do not change kernel for MPTCP.

---

## Compiler Strategy

System GCC:

```text
gcc 13.3
```

Keep it unchanged.

OCUDU:

```text
clang-18 / clang++-18
18.1.3
aarch64-unknown-linux-gnu
```

Future FlexRIC/xApps:

```text
GCC 13.3
```

Do not globally change compiler alternatives.

---

## UHD / USRP B210 #1

UHD:

```text
4.6.0.0
```

B210:

```text
serial: 3271233
name: MyB210
product: B210
type: b200
```

Verified:

```text
uhd_find_devices PASS
uhd_usrp_probe PASS
Operating over USB 3
register loopback PASS
```

Benchmark:

```text
30.72 MS/s RX
20 s
0 drop
0 overrun
0 timeout
```

B210 may remain physically connected over USB when the experiment is stopped.

Do not run UHD benchmark/probe that opens the radio while OCUDU DU is actively using the B210.

---

## OCUDU

Repository:

```text
~/ocudu
```

Version:

```text
release_26_04
commit 050a2bb
OCUDU 26.04.0
```

Build directory:

```text
~/ocudu/build
```

Built successfully:

```text
~/ocudu/build/apps/cu/ocu
~/ocudu/build/apps/du_split_8/odu
~/ocudu/build/apps/gnb_split_8/gnb
```

Final topology uses independent CU + DU:

```text
ocu
↓ F1
odu_split_8
```

Not monolithic gNB.

Unit smoke test `band_helper_test` passed.

---

## DU1 Config

File:

```text
~/ocudu/configs/du1_b210_n78_20mhz.yml
```

Current configuration:

```yaml
gnb_du_id: 1

f1ap:
  addrs: 127.0.10.1
  bind_addrs: 127.0.10.2

f1u:
  socket:
    - bind_addr: 127.0.10.2

ru_sdr:
  device_driver: uhd
  device_args: type=b200,serial=3271233,num_recv_frames=64,num_send_frames=64
  srate: 23.04
  otw_format: sc12
  tx_gain: 20
  rx_gain: 40

cell_cfg:
  dl_arfcn: 650000
  band: 78
  channel_bandwidth_MHz: 20
  common_scs: 30
  plmn: "00101"
  tac: 7
  pci: 1

log:
  filename: /tmp/du.log
  all_level: warning
```

Runtime confirmed:

```text
Cell pci=1, bw=20 MHz, 1T1R,
dl_arfcn=650000 (n78),
dl_freq=3750 MHz,
dl_ssb_arfcn=649632,
ul_freq=3750 MHz
```

Current RF gain:

```text
TX gain = 20
RX gain = 40
```

TX red LED on B210 means active RF transmission; it is not an error LED.

---

## CU Config

File:

```text
~/ocudu/configs/cu.yml
```

Current relevant config:

```yaml
cu_cp:
  amf:
    addrs: 127.0.0.5
    bind_addrs: 127.0.0.1
    supported_tracking_areas:
      - tac: 7
        plmn_list:
          - plmn: "00101"
            tai_slice_support_list:
              - sst: 1

  f1ap:
    bind_addrs: 127.0.10.1

cu_up:
  f1u:
    socket:
      - bind_addr: 127.0.10.1
```

Dry-run passed.

---

## Open5GS

Installed:

```text
Open5GS 2.8.0~noble5
```

AMF:

```text
127.0.0.5:38412
```

PLMN/TAC:

```text
MCC 001
MNC 01
PLMN 00101
TAC 7
SST 1
```

AMF config:

```text
/etc/open5gs/amf.yaml
```

Backup:

```text
/etc/open5gs/amf.yaml.bak
```

Runtime previously verified:

```text
gNB-N2 accepted
Number of gNBs is now 1
```

---

## MongoDB

Do NOT use native MongoDB 8.

It is incompatible with the current DGX Spark kernel.

Native service should remain:

```text
disabled
inactive
```

If it shows failed:

```bash
sudo systemctl reset-failed mongod
```

Actual database:

```text
MongoDB 7 Docker container
```

Container:

```text
open5gs-mongo
```

Image:

```text
mongo:7.0-jammy
```

Architecture:

```text
arm64
```

Mapping:

```text
127.0.0.1:27017 → container:27017
```

Volume:

```text
open5gs-mongo-data
```

Ping was verified:

```text
{ ok: 1 }
```

---

## Verified Runtime Chain

Already successfully tested:

```text
MongoDB 7
   ↓
Open5GS
   ↓ N2/SCTP
OCUDU CU
   ↓ F1-C/SCTP
OCUDU DU1
   ↓ UHD
B210 #1
```

CU runtime:

```text
N2: Connection to AMF on 127.0.0.5:38412 completed
F1-C: Listening for new connections on 127.0.10.1:38472
==== CU started ===
```

DU runtime:

```text
Detected Device: B210
Operating over USB 3.
Actually got clock rate 23.040000 MHz.

Cell pci=1, bw=20 MHz, 1T1R,
dl_arfcn=650000 (n78),
dl_freq=3750 MHz,
ul_freq=3750 MHz

F1-C: Connection to CU-CP on 127.0.10.1:38472 completed
==== DU started ===
```

F1 SCTP `ESTAB` was confirmed with `ss`.

No late / underflow / overflow / UHD errors were observed in the current functional test.

Scheduling-priority warnings are known and intentionally deferred to Phase 6.

---

## Current Pause State

The experiment is currently intended to be stopped.

Expected safe paused state:

```text
DU          stopped
CU          stopped
Open5GS     stopped
MongoDB 7   stopped
Docker      active
B210 USB    may remain connected
```

MongoDB container can be restarted with:

```bash
sudo docker start open5gs-mongo
```

---

## Normal Startup Order

```text
1. MongoDB
2. Open5GS
3. CU
4. Verify B210
5. DU
6. Verify N2/F1
7. UE
```

Mongo:

```bash
sudo docker start open5gs-mongo
```

Open5GS:

```bash
sudo systemctl start 'open5gs-*'
```

CU:

```bash
cd ~/ocudu
build/apps/cu/ocu -c configs/cu.yml
```

DU:

```bash
cd ~/ocudu
build/apps/du_split_8/odu -c configs/du1_b210_n78_20mhz.yml
```

---

# NEXT TASK

Continue with:

```text
Phase 4 — UE Attach
```

Do not start DU2 or FlexRIC yet.

First establish one completely working end-to-end path.

Required next sequence:

```text
1. Configure Open5GS subscriber
2. Identify exact Quectel modem / SIM parameters
3. Verify subscriber IMSI/SUPI, K, OP/OPc, AMF
4. Configure DNN/APN
5. Verify Open5GS user plane:
   - SMF
   - UPF
   - ogstun
   - N3
   - N6
   - IP forwarding / routing / NAT as needed
6. Start MongoDB
7. Start Open5GS
8. Start CU
9. Start DU1
10. Have Quectel search n78 / PLMN 00101
11. Registration
12. Authentication
13. PDU session
14. Confirm UE IP
15. ping
16. iperf
```

After Phase 4 is stable:

```text
Phase 5:
DU2 + B210 #2

Then:
Phase 6 real-time tuning
Phase 7 FlexRIC
Phase 8 KPM
Phase 9 MPTCP
Phase 10 RIC-assisted MPTCP
Phase 11 AI proactive steering
```

---

## Working Style Requirements

Follow these rules strictly:

1. Give only the next small step.
2. Give exact command(s).
3. State expected output.
4. Wait for real output before deciding the next step.
5. Never assume a command succeeded.
6. Debug from real logs.
7. Do not replace or modify the DGX Spark kernel.
8. Do not change multiple variables simultaneously.
9. Explain why a configuration is changed before changing it.
10. Prefer official OCUDU / Ettus / Open5GS / FlexRIC / NVIDIA documentation.
11. Check current official docs for version-sensitive information.
12. Preserve the current working single-link baseline before extending.
13. Do not enable DU2, FlexRIC, or MPTCP steering until Phase 4 is complete.
14. Keep current B210 #1 fixed to serial `3271233`.
15. Keep current first-cell baseline at:
    - n78
    - 20 MHz
    - 30 kHz SCS
    - SISO
    - PCI 1
    - TX gain 20
    - RX gain 40
16. Do not treat `Scheduling priority ... Not enough privileges` as a blocker yet.
17. Real-time tuning belongs to Phase 6.
18. Do not run uncontrolled OTA RF tests; the current DU transmits around 3.75 GHz.
