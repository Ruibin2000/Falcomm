# 1. 项目进展介绍
# DGX Spark + OCUDU + USRP B210 双链路 5G / AI-RAN Testbed

最后更新：2026-10-02

---

## 一、项目最终目标

在一台 NVIDIA DGX Spark 上搭建：

- OCUDU 5G RAN
- 1 × OCUDU CU
- 2 × OCUDU DU
- 2 × Ettus USRP B210
- Open5GS 5GC
- FlexRIC Near-RT RIC
- RIC xApp
- AI channel/rate predictor
- MPTCP controller / MPTCP server

最终架构：

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

最终控制链：

```text
DU1 / DU2
   ↓ E2
FlexRIC
   ↓
xApp
   ↓
AI / policy controller
   ↓
Linux MPTCP path manager / scheduler
```

---

# 二、当前整体进度

| Phase | 内容 | 状态 |
|---|---|---|
| Phase 0 | DGX Spark baseline | ✅ 完成 |
| Phase 1 | UHD + B210 #1 | ✅ 完成 |
| Phase 2 | OCUDU build | ✅ 完成 |
| Phase 3 | 单 CU + 单 DU1 + B210 #1 | ✅ 完成 |
| Phase 3 | Open5GS 基础部署 | ✅ 完成 |
| Phase 4 | UE subscriber + attach | ⏳ 下一步 |
| Phase 5 | 第二个 DU + B210 #2 | ⏳ 未开始 |
| Phase 6 | Real-time tuning | ⏳ 未开始 |
| Phase 7 | FlexRIC | ⏳ 未开始 |
| Phase 8 | KPM xApp | ⏳ 未开始 |
| Phase 9 | MPTCP 双 path | ⏳ 未开始 |
| Phase 10 | RIC-assisted MPTCP | ⏳ 未开始 |
| Phase 11 | AI proactive steering | ⏳ 未开始 |

当前已经实际跑通：

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

# 三、DGX Spark Baseline

```text
Architecture: aarch64
Ubuntu:       24.04.5 LTS
Kernel:       7.0.0-1019-nvidia
Kernel mode:  PREEMPT_DYNAMIC
CPU count:    20
NUMA nodes:   1
RAM:          121 GiB
```

CPU：

```text
Cortex-A725:
  CPU 0-4, 10-14
  Max 2.808 GHz

Cortex-X925:
  CPU 5-9, 15-19
  Max 3.9 GHz
```

目前尚未做：

```text
isolcpus
nohz_full
rcu_nocbs
idle=poll
IRQ affinity
CPU pinning
RT priority capability
```

这是刻意的：先跑通功能，再做 Phase 6 实时优化。

---

# 四、MPTCP Kernel 状态

当前 NVIDIA kernel 已原生支持：

```text
CONFIG_MPTCP=y
CONFIG_MPTCP_IPV6=y
net.mptcp.enabled = 1
```

结论：

- 不需要为了 MPTCP 换 kernel。
- 不需要关闭 MPTCP。
- 以后只配置 MPTCP endpoint/path manager/scheduler。

---

# 五、编译器策略

系统默认：

```text
gcc / g++ 13.3
```

OCUDU 使用：

```text
clang-18 / clang++-18
Version: 18.1.3
Target: aarch64-unknown-linux-gnu
```

计划：

```text
OCUDU      → Clang 18
FlexRIC    → GCC 13.3
xApp       → GCC 13.3
Open5GS    → Ubuntu binary package
AI         → Python / CUDA
MPTCP      → NVIDIA Linux kernel + userspace tools
```

未修改 `/usr/bin/gcc` 默认版本。

---

# 六、UHD / B210 #1

UHD：

```text
4.6.0.0+ds1-5.1ubuntu0.24.04.1
```

B210 #1：

```text
Product: B210
Name:    MyB210
Serial:  3271233
Type:    b200
```

已验证：

```text
uhd_find_devices       PASS
uhd_usrp_probe         PASS
USB 3                  PASS
Register loopback      PASS
```

USB streaming benchmark：

```text
RX rate: 30.72 MS/s
Duration: 20 s

Dropped:       0
Overruns:      0
Seq errors Rx: 0
Timeouts Rx:   0
```

注意：

30.72 MS/s 只是 USB/UHD 压力测试。

OCUDU 当前实际配置：

```text
srate: 23.04 MS/s
```

---

# 七、OCUDU 版本与 Build

源码：

```text
~/ocudu
```

版本：

```text
Tag:     release_26_04
Commit:  050a2bb
Version: 26.04.0
```

Build dir：

```text
~/ocudu/build
```

关键 binary：

```text
~/ocudu/build/apps/cu/ocu
~/ocudu/build/apps/du_split_8/odu
~/ocudu/build/apps/gnb_split_8/gnb
```

最终架构使用：

```text
ocu
+
odu_split_8
```

`gnb_split_8` 仅用于 smoke build，不作为最终拓扑。

已通过：

```text
band_helper_test
100% tests passed
0 failed
```

---

# 八、DU1 当前配置

文件：

```text
~/ocudu/configs/du1_b210_n78_20mhz.yml
```

当前配置：

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

当前 RF：

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

实际启动已确认：

```text
Cell pci=1, bw=20 MHz, 1T1R,
dl_arfcn=650000 (n78),
dl_freq=3750 MHz,
dl_ssb_arfcn=649632,
ul_freq=3750 MHz
```

---

# 九、CU 当前配置

文件：

```text
~/ocudu/configs/cu.yml
```

关键配置：

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

地址：

```text
CU → AMF N2:
127.0.0.1 → 127.0.0.5:38412

CU ↔ DU1 F1:
CU:  127.0.10.1
DU1: 127.0.10.2
```

---

# 十、Open5GS

版本：

```text
Open5GS 2.8.0~noble5
```

来源：

```text
ppa:open5gs/latest
Ubuntu Noble arm64
```

AMF：

```text
127.0.0.5:38412
```

5GC identity：

```text
PLMN = 00101
MCC  = 001
MNC  = 01
TAC  = 7
SST  = 1
```

AMF 配置：

```text
/etc/open5gs/amf.yaml
```

备份：

```text
/etc/open5gs/amf.yaml.bak
```

AMF 已验证：

```text
gNB-N2 accepted
Number of gNBs is now 1
```

---

# 十一、MongoDB

原生 MongoDB 8 与当前 DGX Spark kernel 不兼容。

原生服务：

```text
mongod.service
disabled
```

如果 systemd 显示 failed，只需要：

```bash
sudo systemctl reset-failed mongod
```

不要启动原生 MongoDB 8。

实际使用：

```text
MongoDB 7
Docker container
```

容器：

```text
Name:  open5gs-mongo
Image: mongo:7.0-jammy
Arch:  arm64
```

映射：

```text
127.0.0.1:27017 → container:27017
```

Persistent volume：

```text
open5gs-mongo-data
```

已验证：

```text
{ ok: 1 }
```

---

# 十二、已经验证的 Runtime Chain

## Open5GS ↔ CU

CU：

```text
N2: Connection to AMF on 127.0.0.5:38412 completed
```

AMF：

```text
gNB-N2 accepted
Number of gNBs is now 1
```

PASS。

## CU ↔ DU1

CU：

```text
127.0.10.1:38472
```

DU：

```text
F1-C: Connection to CU-CP on 127.0.10.1:38472 completed
```

`ss` 已确认 SCTP `ESTAB`。

PASS。

## DU1 ↔ B210

DU：

```text
Detected Device: B210
Operating over USB 3.
Register loopback test passed
Clock rate: 23.04 MHz
==== DU started ===
```

PASS。

---

# 十三、当前 Known Warning

启动 CU/DU 时：

```text
Scheduling priority ... Not enough privileges
```

目前不处理。

计划 Phase 6 再做：

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

# 十四、RF 注意事项

当前 DU 启动后，B210 会实际发射：

```text
~3.75 GHz
n78
TX gain = 20
```

B210 RF A TX 红灯表示正在发送，不是错误灯。

即使没有 UE，gNB 仍会发送：

```text
PSS
SSS
PBCH
SSB
broadcast/system information related signals
```

当前 B210 可以一直通过 USB 接在 Spark 上。

暂停实验时不需要拔 USB，只需要停掉 DU。

不要在未经授权的开放环境直接 OTA 发射。

---

# 十五、当前暂停状态

建议暂停：

```text
DU           stopped
CU           stopped
Open5GS      stopped
MongoDB 7    stopped
Docker       active
B210 USB     connected is OK
```

当前已经确认 MongoDB container 停止时：

```bash
sudo docker ps --filter name=open5gs-mongo
```

为空是正常的。

容器本身应通过以下命令确认仍存在：

```bash
sudo docker ps -a --filter name=open5gs-mongo
```

---

# 十六、下一阶段

下一步：

```text
Phase 4 — UE attach
```

顺序：

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

单链路 UE 完成后，再进入：

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
