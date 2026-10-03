# 3. Reproducible Installation Procedure for the Current DGX Spark Baseline

Objective: reproduce the currently validated configuration on a clean DGX Spark system:

```text
Open5GS
   ↓ N2
OCUDU CU
   ↓ F1
OCUDU DU
   ↓ UHD
USRP B210
```

Platform:

```text
DGX Spark
Ubuntu 24.04.5
aarch64
NVIDIA kernel 7.0.0-1019-nvidia
```

Constraints:

- Retain the NVIDIA kernel.
- Do not install PREEMPT_RT.
- Do not change the system-wide GCC configuration.
- Build OCUDU with Clang 18.
- Use the system GCC 13.3 for future FlexRIC work.
- Use MongoDB 7 in Docker; do not use native MongoDB 8.

---

# Phase 0 — System Baseline

Inspect the operating system and kernel:

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

# Phase 1 — UHD and B210

Install UHD packages:

```bash
sudo apt install libuhd-dev uhd-host
```

Check the installed version:

```bash
uhd_config_info --version
```

Download the UHD images:

```bash
sudo uhd_images_downloader
```

If device access is denied for an unprivileged user, inspect the udev rule:

```bash
dpkg -L uhd-host | grep -i udev
grep -nE '2500|0020|B200|B210|MODE' \
  /usr/lib/udev/rules.d/60-uhd-host.rules
```

Reload：

```bash
sudo udevadm control --reload-rules
```

After reconnecting the B210, run:

```bash
uhd_find_devices
```

Recorded B210 identity:

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

Expected output includes:

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

Acceptance criteria:

```text
drop = 0
overrun = 0
timeout = 0
```

---

# Phase 2 — Build OCUDU

Install Clang 18:

```bash
sudo apt install clang-18
```

Verify the compiler version:

```bash
clang-18 --version
clang++-18 --version
```

Install dependencies:

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

Verify the repository state and latest commit:

```bash
cd ~/ocudu
git status
git log -1 --oneline
```

Expected revision:

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

Note:

The `release_26_04` build does not use the `DU_SPLIT_TYPE` CMake variable.

Build the CU:

```bash
cmake --build build \
  --target ocu \
  -j 8
```

Build the Split 8 DU:

```bash
cmake --build build \
  --target odu_split_8 \
  -j 8
```

Optionally build the monolithic gNB for a smoke test:

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

# Phase 3 — Configure the DU

Start from the upstream template:

```bash
cd ~/ocudu

cp configs/du_rf_b200_tdd_n78_20mhz.yml \
   configs/du1_b210_n78_20mhz.yml
```

Set the DU identifier:

```yaml
gnb_du_id: 1
```

Select B210 #1 by serial number:

```yaml
device_args: type=b200,serial=3271233,num_recv_frames=64,num_send_frames=64
```

The resulting key configuration is:

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

# Phase 3 — Configure the CU

Configuration file:

```text
~/ocudu/configs/cu.yml
```

Change the AMF address from:

```text
127.0.1.100
```

to:

```text
127.0.0.5
```

The relevant configuration is:

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

# Phase 3 — Configure MongoDB

Native MongoDB 8 does not start on the NVIDIA 7.0 kernel in the recorded deployment.

If it is already installed, disable it and clear its failed state:

```bash
sudo systemctl disable --now mongod
sudo systemctl reset-failed mongod
```

Confirm that it remains:

```text
disabled
inactive
```

Use Docker for MongoDB 7:

```bash
sudo docker pull mongo:7.0-jammy
```

Verify the image architecture:

```bash
sudo docker image inspect mongo:7.0-jammy \
  --format '{{.Architecture}} {{.Os}}'
```

Expected output:

```text
arm64 linux
```

Create and start the container:

```bash
sudo docker run -d \
  --name open5gs-mongo \
  -p 127.0.0.1:27017:27017 \
  -v open5gs-mongo-data:/data/db \
  mongo:7.0-jammy
```

Test database connectivity:

```bash
sudo docker exec open5gs-mongo \
  mongosh --quiet --eval 'db.adminCommand({ping: 1})'
```

---

# Phase 3 — Configure Open5GS

Add the package archive:

```bash
sudo add-apt-repository ppa:open5gs/latest
```

Install Open5GS:

```bash
sudo apt install --no-install-recommends open5gs
```

Recorded version:

```text
2.8.0~noble5
```

Edit:

```text
/etc/open5gs/amf.yaml
```

Replace the default values:

```text
MCC/MNC = 999/70
TAC = 1
```

with:

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

Restart the AMF:

```bash
sudo systemctl restart open5gs-amfd
```

Verify the N2 listener:

```bash
sudo ss -lnp | grep 38412
```

Expected listener:

```text
127.0.0.5:38412
```

---

# Phase 3 — Runtime Verification

Start MongoDB:

```bash
sudo docker start open5gs-mongo
```

Start Open5GS:

```bash
sudo systemctl start 'open5gs-*'
```

Start the CU:

```bash
cd ~/ocudu

build/apps/cu/ocu \
  -c configs/cu.yml
```

Verify that the CU reports:

```text
N2: Connection to AMF on 127.0.0.5:38412 completed
```

AMF：

```bash
journalctl -u open5gs-amfd -n 50 --no-pager | \
grep -Ei 'gNB|accepted'
```

Expected AMF log entries:

```text
gNB-N2 accepted
Number of gNBs is now 1
```

Start the DU:

```bash
cd ~/ocudu

build/apps/du_split_8/odu \
  -c configs/du1_b210_n78_20mhz.yml
```

Verify that the DU reports:

```text
Detected Device: B210
Operating over USB 3.
Cell pci=1, bw=20 MHz, 1T1R
F1-C: Connection to CU-CP on 127.0.10.1:38472 completed
==== DU started ===
```

Verify the F1 association:

```bash
sudo ss -anp | grep 38472
```

The association should be in the `ESTAB` state.

---

# Reproduction Endpoint

At this point, the currently completed configuration has been reproduced:

```text
Open5GS
   ↕ N2
OCUDU CU
   ↕ F1
OCUDU DU1
   ↕ UHD
B210 #1
```

The next step is UE attachment and end-to-end validation:

```text
Phase 4
Open5GS subscriber
→ Quectel
→ Registration
→ PDU session
→ ping
→ iperf
```
