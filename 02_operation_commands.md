# 2. 操作指令手册
# DGX Spark + Open5GS + OCUDU CU/DU + B210

---

# 一、重要路径

```text
OCUDU repo:
~/ocudu

CU:
~/ocudu/build/apps/cu/ocu

DU:
~/ocudu/build/apps/du_split_8/odu

CU config:
~/ocudu/configs/cu.yml

DU config:
~/ocudu/configs/du1_b210_n78_20mhz.yml

Open5GS AMF:
 /etc/open5gs/amf.yaml

MongoDB Docker:
 open5gs-mongo
```

---

# 二、正常启动顺序

```text
1 MongoDB
2 Open5GS
3 OCUDU CU
4 检查 B210
5 OCUDU DU
6 验证 N2 / F1 / UHD
7 UE
```

---

# 三、启动 MongoDB

```bash
sudo docker start open5gs-mongo
```

检查：

```bash
sudo docker ps --filter name=open5gs-mongo
```

Ping：

```bash
sudo docker exec open5gs-mongo \
  mongosh --quiet --eval 'db.adminCommand({ping: 1})'
```

预期：

```text
{ ok: 1 }
```

日志：

```bash
sudo docker logs --tail 50 open5gs-mongo
```

---

# 四、启动 Open5GS

简单方式：

```bash
sudo systemctl start 'open5gs-*'
```

如果 shell 不接受 glob，则使用：

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

查看：

```bash
systemctl --no-pager --full list-units 'open5gs-*' \
  --type=service --all
```

AMF：

```bash
systemctl --no-pager --full status open5gs-amfd
```

---

# 五、检查 AMF N2

```bash
sudo ss -lnp | grep 38412
```

应看到：

```text
127.0.0.5:38412
open5gs-amfd
```

也可：

```bash
sudo ss -lnpA sctp
```

---

# 六、启动 OCUDU CU

Terminal 1：

```bash
cd ~/ocudu

build/apps/cu/ocu \
  -c configs/cu.yml
```

预期：

```text
N2: Connection to AMF on 127.0.0.5:38412 completed
F1-C: Listening for new connections on bind addresses 127.0.10.1, port 38472...
==== CU started ===
```

---

# 七、确认 AMF 接收到 CU

```bash
journalctl -u open5gs-amfd -n 50 --no-pager | \
grep -Ei 'gNB|NGAP|NG Setup|SCTP|accepted'
```

预期：

```text
gNB-N2 accepted
Number of gNBs is now 1
```

---

# 八、检查 B210

```bash
uhd_find_devices
```

预期：

```text
serial: 3271233
product: B210
type: b200
```

完整 probe：

```bash
uhd_usrp_probe --args="type=b200,serial=3271233"
```

USB：

```bash
lsusb -t
```

加载 firmware 后应工作在：

```text
5000M
```

---

# 九、启动 DU1

注意：

当前会在 ~3.75 GHz 发射 RF。

Terminal 2：

```bash
cd ~/ocudu

build/apps/du_split_8/odu \
  -c configs/du1_b210_n78_20mhz.yml
```

预期：

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

---

# 十、验证 F1

```bash
sudo ss -anp | grep 38472
```

应看到：

```text
127.0.10.1:38472
       ↕ ESTAB
127.0.10.2:<port>
```

---

# 十一、检查 CU/DU 错误

```bash
grep -Ei 'F1|error|warning|late|underflow|overflow|radio|UHD' \
  /tmp/cu.log /tmp/du.log | tail -80
```

查看尾部：

```bash
tail -30 /tmp/cu.log
tail -30 /tmp/du.log
```

---

# 十二、停止实验

顺序：

```text
DU
↓
CU
↓
Open5GS
↓
MongoDB
```

DU terminal：

```text
Ctrl+C
```

CU terminal：

```text
Ctrl+C
```

停止 Open5GS：

```bash
sudo systemctl stop 'open5gs-*'
```

如果 glob 不工作：

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

停止 MongoDB：

```bash
sudo docker stop open5gs-mongo
```

Docker daemon 不用关。

---

# 十三、确认暂停状态

Open5GS：

```bash
systemctl --no-pager list-units 'open5gs-*' \
  --type=service --state=running
```

MongoDB container：

```bash
sudo docker ps --filter name=open5gs-mongo
```

容器仍存在：

```bash
sudo docker ps -a --filter name=open5gs-mongo
```

Docker：

```bash
systemctl is-active docker
```

应为：

```text
active
```

---

# 十四、原生 MongoDB 8

不要使用：

```bash
sudo systemctl start mongod
```

应保持：

```bash
systemctl is-enabled mongod
systemctl is-active mongod
```

理想：

```text
disabled
inactive
```

如果显示：

```text
disabled
failed
```

清状态：

```bash
sudo systemctl reset-failed mongod
```

---

# 十五、Dry-run

CU：

```bash
cd ~/ocudu

build/apps/cu/ocu \
  --dryrun \
  -c configs/cu.yml
```

DU：

```bash
cd ~/ocudu

build/apps/du_split_8/odu \
  --dryrun \
  -c configs/du1_b210_n78_20mhz.yml
```

---

# 十六、Rebuild

CU：

```bash
cd ~/ocudu

cmake --build build \
  --target ocu \
  -j 8
```

DU：

```bash
cd ~/ocudu

cmake --build build \
  --target odu_split_8 \
  -j 8
```

---

# 十七、UHD Benchmark

不要在 DU 正在占用 B210 时运行。

```bash
/usr/libexec/uhd/examples/benchmark_rate \
  --args="type=b200,serial=3271233" \
  --rx_rate 30.72e6 \
  --duration 20
```

---

# 十八、修改 TX/RX Gain

当前：

```yaml
tx_gain: 20
rx_gain: 40
```

检查：

```bash
grep -nE 'tx_gain|rx_gain' \
  ~/ocudu/configs/du1_b210_n78_20mhz.yml
```

修改 TX gain 示例：

```bash
sed -i 's/tx_gain: 20/tx_gain: 30/' \
  ~/ocudu/configs/du1_b210_n78_20mhz.yml
```

不需要重新编译。

---

# 十九、MPTCP 状态

```bash
grep CONFIG_MPTCP /boot/config-$(uname -r)
sysctl net.mptcp.enabled
```

预期：

```text
CONFIG_MPTCP=y
CONFIG_MPTCP_IPV6=y
net.mptcp.enabled = 1
```

目前尚未配置 endpoint。

---

# 二十、快速诊断命令

进程：

```bash
ps aux | grep -E 'ocu|odu|open5gs|mongod' | grep -v grep
```

SCTP：

```bash
sudo ss -anpA sctp
```

N2：

```bash
sudo ss -anp | grep 38412
```

F1：

```bash
sudo ss -anp | grep 38472
```

AMF log：

```bash
journalctl -u open5gs-amfd -n 100 --no-pager
```

Mongo：

```bash
sudo docker ps -a --filter name=open5gs-mongo
sudo docker logs --tail 50 open5gs-mongo
```

B210：

```bash
uhd_find_devices
uhd_usrp_probe --args="type=b200,serial=3271233"
lsusb -t
```
