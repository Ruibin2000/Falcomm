# 1. Project Progress
# DGX Spark + OCUDU + USRP B210 Dual-Link 5G/AI-RAN Testbed

Last updated: 2026-10-02

---

## I. Project Objective

The project aims to build the following system on a single NVIDIA DGX Spark:

- OCUDU 5G RAN
- 1 × OCUDU CU
- 2 × OCUDU DU
- 2 × Ettus USRP B210
- 2 × Quectel UE modems and one UE host with an MPTCP client and two IP interfaces
- Pi-Radio frequency converters for the approximately 7 GHz FR3 OTA path
- Open5GS 5GC
- FlexRIC Near-RT RIC
- RIC xApp
- Server application and MPTCP server

The end-to-end target topology, including the UE side, FR1 and FR3 OTA paths, network side, RIC, and MPTCP server, is specified in the [README target architecture](README.md#target-architecture). The FR1 OTA path is centered at 3.5 GHz. The FR3 path uses Pi-Radio frequency conversion between 3.5 GHz equipment interfaces and an approximately 7 GHz OTA link. RAN measurements are delivered to FlexRIC over E2 and used to inform server-side MPTCP path steering.

---

# II. Overall Project Status

| Phase | Description | Status |
|---|---|---|
| Phase 0 | DGX Spark baseline | ✅ Complete |
| Phase 1 | UHD and B210 #1 | ✅ Complete |
| Phase 2 | OCUDU build | ✅ Complete |
| Phase 3 | One CU, one DU1, and B210 #1 | ✅ Complete |
| Phase 3 | Basic Open5GS deployment | ✅ Complete |
| Phase 4 | UE subscriber provisioning and attachment | ⏳ Next |
| Phase 5 | Second DU and B210 #2 | ⏳ Not started |
| Phase 6 | Real-time tuning | ⏳ Not started |
| Phase 7 | FlexRIC | ⏳ Not started |
| Phase 8 | KPM xApp | ⏳ Not started |
| Phase 9 | Dual-path MPTCP | ⏳ Not started |
| Phase 10 | RIC-assisted MPTCP | ⏳ Not started |
| Phase 11 | AI-based proactive steering | ⏳ Not started |

The following runtime chain has been demonstrated:

```text
MongoDB 7
   │
Open5GS
   │ N2 / SCTP
OCUDU CU
   │ F1-C
OCUDU DU1
   │ UHD
USRP B210 #1
```

---

# III. DGX Spark Baseline

```text
Architecture: aarch64
Ubuntu:       24.04.5 LTS
Kernel:       7.0.0-1019-nvidia
Kernel mode:  PREEMPT_DYNAMIC
CPU count:    20
NUMA nodes:   1
RAM:          121 GiB
```

CPU:

```text
Cortex-A725:
  CPU 0-4, 10-14
  Max 2.808 GHz

Cortex-X925:
  CPU 5-9, 15-19
  Max 3.9 GHz
```

The following real-time optimizations have not been applied:

```text
isolcpus
nohz_full
rcu_nocbs
idle=poll
IRQ affinity
CPU pinning
RT priority capability
```

This is intentional: functional validation precedes the Phase 6 real-time optimization work.

---

# IV. MPTCP Kernel Status

The current NVIDIA kernel provides native support for:

```text
CONFIG_MPTCP=y
CONFIG_MPTCP_IPV6=y
net.mptcp.enabled = 1
```

Consequently:

- No kernel replacement is required for MPTCP.
- MPTCP does not need to be disabled.
- Subsequent work should configure MPTCP endpoints, the path manager, and the scheduler.

---

# V. Compiler Strategy

System default:

```text
gcc / g++ 13.3
```

OCUDU is built with:

```text
clang-18 / clang++-18
Version: 18.1.3
Target: aarch64-unknown-linux-gnu
```

Planned compiler assignments:

```text
OCUDU      → Clang 18
FlexRIC    → GCC 13.3
xApp       → GCC 13.3
Open5GS    → Ubuntu binary package
AI         → Python / CUDA
MPTCP      → NVIDIA Linux kernel + userspace tools
```

The default `/usr/bin/gcc` selection has not been modified.

---

# VI. UHD and B210 #1

UHD:

```text
4.6.0.0+ds1-5.1ubuntu0.24.04.1
```

B210 #1:

```text
Product: B210
Name:    MyB210
Serial:  3271233
Type:    b200
```

The following checks passed:

```text
uhd_find_devices       PASS
uhd_usrp_probe         PASS
USB 3                  PASS
Register loopback      PASS
```

USB streaming benchmark:

```text
RX rate: 30.72 MS/s
Duration: 20 s

Dropped:       0
Overruns:      0
Seq errors Rx: 0
Timeouts Rx:   0
```

Note:

The 30.72 MS/s measurement is a USB/UHD stress test only.

The current OCUDU configuration uses:

```text
srate: 23.04 MS/s
```

---

# VII. OCUDU Version and Build

Source repository:

```text
~/ocudu
```

Version:

```text
Tag:     release_26_04
Commit:  050a2bb
Version: 26.04.0
```

Build dir:

```text
~/ocudu/build
```

Key binaries:

```text
~/ocudu/build/apps/cu/ocu
~/ocudu/build/apps/du_split_8/odu
~/ocudu/build/apps/gnb_split_8/gnb
```

The target split architecture uses:

```text
ocu
+
odu_split_8
```

`gnb_split_8` was built for a smoke test and is not part of the target topology.

The following test passed:

```text
band_helper_test
100% tests passed
0 failed
```

---

# VIII. Current DU1 Configuration

Configuration file:

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

Current RF parameters:

```text
DU ID:      1
Band:       n78
ARFCN:      650000
Frequency:  3750 MHz
BW:         20 MHz
SCS:        30 kHz
SISO:       1T1R
PCI:        1
TX gain:    20
RX gain:    40
```

Runtime startup confirmed:

```text
Cell pci=1, bw=20 MHz, 1T1R,
dl_arfcn=650000 (n78),
dl_freq=3750 MHz,
dl_ssb_arfcn=649632,
ul_freq=3750 MHz
```

---

# IX. Current CU Configuration

Configuration file:

```text
~/ocudu/configs/cu.yml
```

Relevant configuration:

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

Addresses:

```text
CU → AMF N2:
127.0.0.1 → 127.0.0.5:38412

CU ↔ DU1 F1:
CU:  127.0.10.1
DU1: 127.0.10.2
```

---

# X. Open5GS

Version:

```text
Open5GS 2.8.0~noble5
```

Package source:

```text
ppa:open5gs/latest
Ubuntu Noble arm64
```

AMF:

```text
127.0.0.5:38412
```

5GC identity:

```text
PLMN = 00101
MCC  = 001
MNC  = 01
TAC  = 7
SST  = 1
```

AMF configuration:

```text
/etc/open5gs/amf.yaml
```

Backup:

```text
/etc/open5gs/amf.yaml.bak
```

The AMF was verified to report:

```text
gNB-N2 accepted
Number of gNBs is now 1
```

---

# XI. MongoDB

Native MongoDB 8 is incompatible with the current DGX Spark kernel.

Native service:

```text
mongod.service
disabled
```

If systemd reports a failed state, clear it with:

```bash
sudo systemctl reset-failed mongod
```

Do not start native MongoDB 8.

The deployed database is:

```text
MongoDB 7
Docker container
```

Container:

```text
Name:  open5gs-mongo
Image: mongo:7.0-jammy
Arch:  arm64
```

Port mapping:

```text
127.0.0.1:27017 → container:27017
```

Persistent volume:

```text
open5gs-mongo-data
```

The following ping result was verified:

```text
{ ok: 1 }
```

---

# XII. Verified Runtime Chain

## Open5GS ↔ CU

CU:

```text
N2: Connection to AMF on 127.0.0.5:38412 completed
```

AMF:

```text
gNB-N2 accepted
Number of gNBs is now 1
```

PASS.

## CU ↔ DU1

CU:

```text
127.0.10.1:38472
```

DU:

```text
F1-C: Connection to CU-CP on 127.0.10.1:38472 completed
```

The SCTP association was confirmed in the `ESTAB` state using `ss`.

PASS.

## DU1 ↔ B210

DU:

```text
Detected Device: B210
Operating over USB 3.
Register loopback test passed
Clock rate: 23.04 MHz
==== DU started ===
```

PASS.

---

# XIII. Known Runtime Warning

When starting the CU/DU, the following warning may appear:

```text
Scheduling priority ... Not enough privileges
```

This warning is deferred at the current stage.

The following items are planned for Phase 6:

```text
CAP_SYS_NICE / RT scheduling
CPU isolation
CPU affinity
IRQ affinity
nohz_full
rcu_nocbs
idle / C-state tuning
```

---

# XIV. RF Operating Considerations

When the current DU is active, the B210 transmits at:

```text
~3.75 GHz
n78
TX gain = 20
```

The red RF A TX indicator on the B210 denotes active transmission; it is not an error indicator.

The gNB transmits the following signals even when no UE is present:

```text
PSS
SSS
PBCH
SSB
broadcast/system information related signals
```

The B210 may remain connected to the Spark over USB.

To pause the experiment, stop the DU; disconnecting the USB cable is unnecessary.

Do not conduct over-the-air transmission in an unlicensed or otherwise unauthorized environment.

---

# XV. Current Paused State

The intended paused state is:

```text
DU           stopped
CU           stopped
Open5GS      stopped
MongoDB 7    stopped
Docker       active
B210 USB     connected is OK
```

When the MongoDB container is stopped, the following command should return:

```bash
sudo docker ps --filter name=open5gs-mongo
```

no running container; this is expected.

Confirm that the container still exists with:

```bash
sudo docker ps -a --filter name=open5gs-mongo
```

---

# XVI. Next Phase

The next task is:

```text
Phase 4 — UE attach
```

Planned sequence:

```text
Open5GS subscriber
        ↓
Quectel #1
        ↓
cell search
        ↓
registration
        ↓
authentication
        ↓
PDU session
        ↓
UE IP
        ↓
ping
        ↓
iperf
```

After validating UE operation on the single link, proceed to:

```text
DU2 + B210 #2
→ dual cell
→ real-time tuning
→ FlexRIC
→ KPM xApp
→ MPTCP
→ RIC-assisted MPTCP
→ AI proactive steering
```
