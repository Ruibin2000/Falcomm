# 3. 从全新 DGX Spark 复现到当前状态的安装步骤

目标：从一台全新的 DGX Spark，复现目前已经跑通的：

```text
Open5GS
   ↓ N2
OCUDU CU
   ↓ F1
OCUDU DU
   ↓ UHD
USRP B210
```

平台：

```text
DGX Spark
Ubuntu 24.04.5
aarch64
NVIDIA kernel 7.0.0-1019-nvidia
```

注意：

- 不替换 NVIDIA kernel。
- 不安装 PREEMPT_RT。
- 不修改全局 GCC。
- OCUDU 使用 Clang 18。
- FlexRIC 以后继续使用系统 GCC 13.3。
- MongoDB 使用 Docker 中的 MongoDB 7，不使用原生 MongoDB 8。

---

# Phase 0 — Baseline

检查：

```bash
uname -m
uname -r
uname -a
cat /etc/os-release
```

CPU：

```bash
lscpu
cat /proc/cmdline
lscpu -e=CPU,CORE,ONLINE,MAXMHZ,MINMHZ
```

Toolchain：

```bash
gcc --version
cmake --version
make --version
```

Memory：

```bash
free -h
grep -i huge /proc/meminfo
```

USB：

```bash
lsusb
lsusb -t
```

MPTCP：

```bash
grep CONFIG_MPTCP /boot/config-$(uname -r)
sysctl net.mptcp.enabled
```

---

# Phase 1 — UHD + B210

安装：

```bash
sudo apt install libuhd-dev uhd-host
```

确认版本：

```bash
uhd_config_info --version
```

下载 UHD image：

```bash
sudo uhd_images_downloader
```

如果普通用户权限不足，确认 rule：

```bash
dpkg -L uhd-host | grep -i udev
grep -nE '2500|0020|B200|B210|MODE' \
  /usr/lib/udev/rules.d/60-uhd-host.rules
```

Reload：

```bash
sudo udevadm control --reload-rules
```

拔插 B210 后：

```bash
uhd_find_devices
```

当前 B210：

```text
serial: 3271233
name: MyB210
product: B210
type: b200
```

Probe：

```bash
uhd_usrp_probe --args="type=b200,serial=3271233"
```

应看到：

```text
Operating over USB 3.
Register loopback test passed
```

Streaming：

```bash
/usr/libexec/uhd/examples/benchmark_rate \
  --args="type=b200,serial=3271233" \
  --rx_rate 30.72e6 \
  --duration 20
```

目标：

```text
drop = 0
overrun = 0
timeout = 0
```

---

# Phase 2 — OCUDU Build

安装 Clang 18：

```bash
sudo apt install clang-18
```

确认：

```bash
clang-18 --version
clang++-18 --version
```

安装依赖：

```bash
sudo apt install \
  libmbedtls-dev \
  libsctp-dev \
  libyaml-cpp-dev \
  libfftw3-dev \
  libgtest-dev \
  pkg-config
```

Clone：

```bash
cd ~

git clone \
  --branch release_26_04 \
  --depth 1 \
  https://gitlab.com/ocudu/ocudu.git
```

确认：

```bash
cd ~/ocudu
git status
git log -1 --oneline
```

应为：

```text
tag release_26_04
commit 050a2bb
```

Configure：

```bash
cd ~/ocudu

cmake -S . -B build \
  -DCMAKE_C_COMPILER=clang-18 \
  -DCMAKE_CXX_COMPILER=clang++-18 \
  -DBUILD_TESTING=ON
```

注意：

`release_26_04` 不使用 `DU_SPLIT_TYPE` CMake variable。

编译 CU：

```bash
cmake --build build \
  --target ocu \
  -j 8
```

编译 DU Split 8：

```bash
cmake --build build \
  --target odu_split_8 \
  -j 8
```

可选 smoke gNB：

```bash
cmake --build build \
  --target gnb_split_8 \
  -j 8
```

Test：

```bash
cmake --build build \
  --target band_helper_test \
  -j 8

ctest --test-dir build \
  -R '^band_helper_test$' \
  --output-on-failure
```

---

# Phase 3 — DU Config

基于官方模板：

```bash
cd ~/ocudu

cp configs/du_rf_b200_tdd_n78_20mhz.yml \
   configs/du1_b210_n78_20mhz.yml
```

添加 DU ID：

```yaml
gnb_du_id: 1
```

固定 B210：

```yaml
device_args: type=b200,serial=3271233,num_recv_frames=64,num_send_frames=64
```

当前最终关键配置：

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
```

Dry-run：

```bash
build/apps/du_split_8/odu \
  --dryrun \
  -c configs/du1_b210_n78_20mhz.yml
```

---

# Phase 3 — CU Config

官方文件：

```text
~/ocudu/configs/cu.yml
```

把 AMF：

```text
127.0.1.100
```

修改为：

```text
127.0.0.5
```

当前：

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

Dry-run：

```bash
build/apps/cu/ocu \
  --dryrun \
  -c configs/cu.yml
```

---

# Phase 3 — MongoDB

原生 MongoDB 8 在 NVIDIA kernel 7.0 上无法启动。

如已经安装：

```bash
sudo systemctl disable --now mongod
sudo systemctl reset-failed mongod
```

保持：

```text
disabled
inactive
```

使用 Docker：

```bash
sudo docker pull mongo:7.0-jammy
```

确认：

```bash
sudo docker image inspect mongo:7.0-jammy \
  --format '{{.Architecture}} {{.Os}}'
```

应为：

```text
arm64 linux
```

创建：

```bash
sudo docker run -d \
  --name open5gs-mongo \
  -p 127.0.0.1:27017:27017 \
  -v open5gs-mongo-data:/data/db \
  mongo:7.0-jammy
```

测试：

```bash
sudo docker exec open5gs-mongo \
  mongosh --quiet --eval 'db.adminCommand({ping: 1})'
```

---

# Phase 3 — Open5GS

添加 PPA：

```bash
sudo add-apt-repository ppa:open5gs/latest
```

安装：

```bash
sudo apt install --no-install-recommends open5gs
```

当前版本：

```text
2.8.0~noble5
```

修改：

```text
/etc/open5gs/amf.yaml
```

将默认：

```text
MCC/MNC = 999/70
TAC = 1
```

改为：

```yaml
guami:
  - plmn_id:
      mcc: 001
      mnc: 01

tai:
  - plmn_id:
      mcc: 001
      mnc: 01
    tac: 7

plmn_support:
  - plmn_id:
      mcc: 001
      mnc: 01
    s_nssai:
      - sst: 1
```

重启：

```bash
sudo systemctl restart open5gs-amfd
```

确认：

```bash
sudo ss -lnp | grep 38412
```

应看到：

```text
127.0.0.5:38412
```

---

# Phase 3 — Runtime Verification

启动 Mongo：

```bash
sudo docker start open5gs-mongo
```

启动 Open5GS：

```bash
sudo systemctl start 'open5gs-*'
```

启动 CU：

```bash
cd ~/ocudu

build/apps/cu/ocu \
  -c configs/cu.yml
```

确认：

```text
N2: Connection to AMF on 127.0.0.5:38412 completed
```

AMF：

```bash
journalctl -u open5gs-amfd -n 50 --no-pager | \
grep -Ei 'gNB|accepted'
```

应看到：

```text
gNB-N2 accepted
Number of gNBs is now 1
```

启动 DU：

```bash
cd ~/ocudu

build/apps/du_split_8/odu \
  -c configs/du1_b210_n78_20mhz.yml
```

确认：

```text
Detected Device: B210
Operating over USB 3.
Cell pci=1, bw=20 MHz, 1T1R
F1-C: Connection to CU-CP on 127.0.10.1:38472 completed
==== DU started ===
```

验证 F1：

```bash
sudo ss -anp | grep 38472
```

应出现 `ESTAB`。

---

# 当前复现终点

到这里即复现当前已完成状态：

```text
Open5GS
   ↕ N2
OCUDU CU
   ↕ F1
OCUDU DU1
   ↕ UHD
B210 #1
```

下一步不是安装更多 RAN 组件，而是：

```text
Phase 4
Open5GS subscriber
→ Quectel
→ Registration
→ PDU session
→ ping
→ iperf
```
