# 2. Operational Command Reference
# DGX Spark + Open5GS + OCUDU CU/DU + B210

---

# I. Key Paths

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

# II. Standard Startup Sequence

```text
1 MongoDB
2 Open5GS
3 OCUDU CU
4. Verify B210 availability
5 OCUDU DU
6. Verify N2, F1, and UHD
7 UE
```

---

# III. Start MongoDB

```bash
sudo docker start open5gs-mongo
```

Check status:

```bash
sudo docker ps --filter name=open5gs-mongo
```

Ping：

```bash
sudo docker exec open5gs-mongo \
  mongosh --quiet --eval 'db.adminCommand({ping: 1})'
```

Expected result:

```text
{ ok: 1 }
```

View logs:

```bash
sudo docker logs --tail 50 open5gs-mongo
```

---

# IV. Start Open5GS

Recommended command:

```bash
sudo systemctl start 'open5gs-*'
```

If the shell does not expand the service-name glob, use:

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

Inspect service status:

```bash
systemctl --no-pager --full list-units 'open5gs-*' \
  --type=service --all
```

AMF：

```bash
systemctl --no-pager --full status open5gs-amfd
```

---

# V. Verify AMF N2

```bash
sudo ss -lnp | grep 38412
```

Expected output:

```text
127.0.0.5:38412
open5gs-amfd
```

Alternatively:

```bash
sudo ss -lnpA sctp
```

---

# VI. Start the OCUDU CU

Terminal 1：

```bash
cd ~/ocudu

build/apps/cu/ocu \
  -c configs/cu.yml
```

Expected output:

```text
N2: Connection to AMF on 127.0.0.5:38412 completed
F1-C: Listening for new connections on bind addresses 127.0.10.1, port 38472...
==== CU started ===
```

---

# VII. Confirm CU Registration at the AMF

```bash
journalctl -u open5gs-amfd -n 50 --no-pager | \
grep -Ei 'gNB|NGAP|NG Setup|SCTP|accepted'
```

Expected output:

```text
gNB-N2 accepted
Number of gNBs is now 1
```

---

# VIII. Verify B210

```bash
uhd_find_devices
```

Expected device information:

```text
serial: 3271233
product: B210
type: b200
```

Run a full device probe:

```bash
uhd_usrp_probe --args="type=b200,serial=3271233"
```

USB：

```bash
lsusb -t
```

After firmware loading, the device should operate at:

```text
5000M
```

---

# IX. Start DU1

RF note:

The current configuration transmits RF at approximately 3.75 GHz.

Terminal 2：

```bash
cd ~/ocudu

build/apps/du_split_8/odu \
  -c configs/du1_b210_n78_20mhz.yml
```

Expected output:

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

# X. Verify F1

```bash
sudo ss -anp | grep 38472
```

Expected state:

```text
127.0.10.1:38472
       ↕ ESTAB
127.0.10.2:<port>
```

---

# XI. Inspect CU/DU Errors

```bash
grep -Ei 'F1|error|warning|late|underflow|overflow|radio|UHD' \
  /tmp/cu.log /tmp/du.log | tail -80
```

Inspect the most recent log entries:

```bash
tail -30 /tmp/cu.log
tail -30 /tmp/du.log
```

---

# XII. Stop the Experiment

Shutdown sequence:

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

Stop Open5GS:

```bash
sudo systemctl stop 'open5gs-*'
```

If the service-name glob is not expanded:

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

Stop MongoDB:

```bash
sudo docker stop open5gs-mongo
```

The Docker daemon does not need to be stopped.

---

# XIII. Confirm the Paused State

Open5GS：

```bash
systemctl --no-pager list-units 'open5gs-*' \
  --type=service --state=running
```

MongoDB container：

```bash
sudo docker ps --filter name=open5gs-mongo
```

Confirm that the container still exists:

```bash
sudo docker ps -a --filter name=open5gs-mongo
```

Docker：

```bash
systemctl is-active docker
```

Expected state:

```text
active
```

---

# XIV. Native MongoDB 8

Do not run:

```bash
sudo systemctl start mongod
```

The service should remain:

```bash
systemctl is-enabled mongod
systemctl is-active mongod
```

Expected status:

```text
disabled
inactive
```

If the service reports:

```text
disabled
failed
```

Clear the failed state:

```bash
sudo systemctl reset-failed mongod
```

---

# XV. Dry-Run Validation

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

# XVI. Rebuild

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

# XVII. UHD Benchmark

Do not run this command while the DU is using the B210.

```bash
/usr/libexec/uhd/examples/benchmark_rate \
  --args="type=b200,serial=3271233" \
  --rx_rate 30.72e6 \
  --duration 20
```

---

# XVIII. Modify TX/RX Gain

Current values:

```yaml
tx_gain: 20
rx_gain: 40
```

Check the configuration:

```bash
grep -nE 'tx_gain|rx_gain' \
  ~/ocudu/configs/du1_b210_n78_20mhz.yml
```

Example: modify the TX gain:

```bash
sed -i 's/tx_gain: 20/tx_gain: 30/' \
  ~/ocudu/configs/du1_b210_n78_20mhz.yml
```

Recompilation is not required.

---

# XIX. MPTCP Status

```bash
grep CONFIG_MPTCP /boot/config-$(uname -r)
sysctl net.mptcp.enabled
```

Expected output:

```text
CONFIG_MPTCP=y
CONFIG_MPTCP_IPV6=y
net.mptcp.enabled = 1
```

No MPTCP endpoint has been configured yet.

---

# XX. Quick Diagnostic Commands

Processes:

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
