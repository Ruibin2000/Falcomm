# Spark 快速指令册

更新于 2026-10-07。已安装的 Spark 用此册；新主机见[软件与驱动](software_installation_and_drivers.md)，详细配置见[完整复现流程](full_reproduction_runbook.md)，UE 操作见[UE laptop 指令册](ue_laptop_quick_commands.md)。使用 Bash，逐步执行，出现 `FAIL` 就停在该步骤。

2026-10-07 已验证 **DU 的 E2 Setup、真实 UE 的五项 KPM 测量、正常取消订阅及 xApp 成功退出**。当前 DU 配置是 TX **70** / RX **40**；2026-10-05 的 UE 应用流量 / TCP 结果使用 TX **80** / RX **40**，两次结果不要混为同一射频基线。以下命令读取当前配置，不改增益。

版本：OCUDU `050a2bb`；FlexRIC `73650812`，构建目录 `build-ocudu`，GCC 13.3 / Debug / `E2AP_V3` / `KPM_V3_00` / `NONE_XAPP` / `XAPP_MULTILANGUAGE=OFF`。

检查命令放在检查终端；CU、FlexRIC、DU、iperf 各占一个前台终端。第 6、7 步使用同一个 xApp Bash 终端，变量不会跨终端共享。

## 1 启动前检查

在 Spark 的检查终端执行。**只有 DU 已停止时才运行 UHD 检查**；发现旧 DU/CU 时先按末尾关机步骤处理。

```bash
if pgrep -x odu >/dev/null; then
  printf 'FAIL: DU 正在运行，先停止；不要探测 B210\n'
else
  lsusb -t
  sudo uhd_find_devices
fi
```

确认 B210 `3271233` 和 USB 树中的 `5000M`。

```bash
cd /home/nyu/ocudu
rg 'tx_gain|rx_gain|srate|dl_arfcn|channel_bandwidth_MHz' configs/du1_b210_n78_20mhz.yml
```

预期 TX 70、RX 40、23.04 MS/s、ARFCN 650000、20 MHz。锁小区时 UE 使用 SSB ARFCN **649632**。

## 2 重启后恢复性能设置

```bash
cd /home/nyu/ocudu
sudo ./scripts/ocudu_performance
```

三个提示依次回答 `Y`，然后简短确认：

```bash
awk '$0 != "performance" {bad++} END {printf "CPU governor: %s (%d CPUs)\n", NR && !bad ? "OK" : "FAIL", NR}' /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor
if [ "$(sudo cat /sys/module/drm_kms_helper/parameters/poll)" = N ]; then
  printf 'DRM polling: OK\n'
else
  printf 'DRM polling: FAIL\n'
fi
sysctl -n net.core.rmem_max net.core.wmem_max net.core.rmem_default net.core.wmem_default |
  awk '$0 != 33554432 {bad++} END {print "Network buffers: " (NR == 4 && !bad ? "OK" : "FAIL")}'
```

## 3 MongoDB 与核心网

保留已有 `open5gs-mongo` 及 data volume。Subscriber 应为 SST 1、`internet`、type 1，核对见[完整流程](full_reproduction_runbook.md#5-start-mongodb-and-verify-the-subscriber)。

```bash
sudo docker start open5gs-mongo >/dev/null &&
sudo docker exec open5gs-mongo mongosh --quiet --eval '
const r = db.adminCommand({ping: 1});
if (r.ok !== 1) { print("MongoDB: FAIL"); quit(1); }
print("MongoDB: OK");
'
```

专用 Spark 先停止已有 LTE/可选服务；换主机时省略不存在的 unit：

```bash
sudo systemctl stop open5gs-mmed open5gs-hssd open5gs-pcrfd open5gs-sgwcd open5gs-sgwud
sudo systemctl stop open5gs-bsfd open5gs-seppd
```

十个 SA unit 顺序启动，再逐项检查，只列失败项。多 unit 的一次 `is-active --quiet` 不能判断全部成功。

```bash
CORE_UNITS=(open5gs-nrfd open5gs-scpd open5gs-udrd open5gs-udmd open5gs-ausfd open5gs-pcfd open5gs-nssfd open5gs-upfd open5gs-smfd open5gs-amfd)
for unit in "${CORE_UNITS[@]}"; do
  if ! sudo systemctl start "$unit"; then
    printf 'FAIL: 无法启动 %s\n' "$unit"
    break
  fi
done
active=0
for unit in "${CORE_UNITS[@]}"; do
  if systemctl is-active --quiet "$unit"; then
    active=$((active + 1))
  else
    printf 'FAIL: %s (%s)\n' "$unit" "$(systemctl is-active "$unit")"
  fi
done
printf '5GC active: %d/10\n' "$active"
```

检查已有 `ogstun` 的 UP 标志、MTU 1400、地址及私网路由：

```bash
if ip -o link show dev ogstun | awk '/(<|,)UP(,|>)/ && / mtu 1400 / {ok=1} END {exit !ok}' &&
   ip -o -4 addr show dev ogstun | awk '$4 == "10.45.0.1/16" {ok=1} END {exit !ok}' &&
   ip -4 route show 10.45.0.0/16 | awk '/^10\.45\.0\.0\/16 .*dev ogstun/ {ok=1} END {exit !ok}'; then
  printf 'ogstun: OK (UP, MTU 1400, 10.45.0.1/16, route)\n'
else
  printf 'ogstun: FAIL；检查 networkd 与 99-open5gs 配置\n'
fi
```

## 4 前台启动 CU 和 FlexRIC

**CU 终端**：

```bash
cd /home/nyu/ocudu
sudo build/apps/cu/ocu -c configs/cu.yml
```

确认 N2 `completed`、`==== CU started ===`，保持运行。然后在 **FlexRIC 终端**启动并保留当前日志：

```bash
set -o pipefail
stdbuf -oL -eL /usr/local/bin/flexric/ric/nearRT-RIC -c /usr/local/etc/flexric/ric.conf 2>&1 |
  tee /tmp/flexric-ric.log
```

## 5 启动带 E2 的 DU

已有 `configs/du1_flexric.yml` 开启 E2/KPM 和 metrics；缺失时先按[完整流程](full_reproduction_runbook.md)创建。在 **DU 终端**执行，此步启动 B210 射频：

```bash
cd /home/nyu/ocudu
sudo build/apps/du_split_8/odu \
  -c configs/du1_b210_n78_20mhz.yml \
  -c configs/du1_flexric.yml
```

确认 F1-C / E2AP `completed`、`==== DU started ===`。FlexRIC 应收到本次 DU `E2 SETUP-REQUEST` 并接受 KPM ID `2`。检查终端只看三条 SCTP 连接摘要：

```bash
sudo ss -H -an -A sctp | awk '
  /ESTAB/ && /:38412([[:space:]]|$)/ {n2=1}
  /ESTAB/ && /:38472([[:space:]]|$)/ {f1=1}
  /ESTAB/ && /:36421([[:space:]]|$)/ {e2=1}
  END {
    printf "SCTP: N2=%s F1=%s E2=%s\n", n2 ? "OK" : "FAIL", f1 ? "OK" : "FAIL", e2 ? "OK" : "FAIL"
    exit !(n2 && f1 && e2)
  }'
if [ ! -f /tmp/flexric-ric.log ]; then
  printf 'RIC 日志未保存；在现有 RIC 终端确认接受 KPM ID 2。\n'
elif grep -q 'Accepting RAN function ID 2 with def = ORAN-E2SM-KPM' /tmp/flexric-ric.log; then
  printf 'RIC accepted DU KPM: OK\n'
else
  printf 'RIC accepted DU KPM: FAIL；查看 FlexRIC 当前日志\n'
fi
```

`/tmp/flexric-ric.log` 由第 4 步的 `tee` 创建；此前直接启动 RIC 时没有这个文件，可查看现有 RIC 终端，无需为保存日志重启连接。

## 6 KPM 订阅快速验证

**等本次 DU 被接受后，在 Spark 的 xApp 终端完整执行下面代码块**。示例采集约 10 秒后删除订阅并退出。完整输出留在临时日志，正常只显示一行。`KPM_LOG` 只保存在这个 Bash 终端；第 7 步的日志命令也在同一终端执行。此前直接运行 xApp、不重定向输出的命令不会创建这个日志文件。

```bash
KPM_LOG=$(mktemp /tmp/flexric-kpm.XXXXXX.log)
/usr/local/bin/flexric/xApp/c/xapp_oran_moni \
  -c /usr/local/etc/flexric/xapp_oran_sm.conf >"$KPM_LOG" 2>&1
KPM_EXIT=$?
if [ "$KPM_EXIT" -eq 0 ] &&
   grep -q 'Successfully subscribed to RAN_FUNC_ID 2' "$KPM_LOG" &&
   grep -q 'E42 SUBSCRIPTION DELETE RESPONSE rx' "$KPM_LOG" &&
   grep -q 'Test xApp run SUCCESSFULLY' "$KPM_LOG"; then
  printf 'KPM: OK (订阅、删除、退出)；log=%s\n' "$KPM_LOG"
else
  printf 'KPM: FAIL (exit=%s)；log=%s\n' "$KPM_EXIT" "$KPM_LOG"
  tail -n 30 "$KPM_LOG"
fi
```

没有 UE 测量时，订阅仍可成功。不要把上述 `OK` 当成 UE KPM 数据验证通过。

## 7 真实 UE 流量与 KPM 验证

本次已收到 UE `gnb_cu_ue_f1ap=14` 的五项 DU 测量。以下用于后续与应用流量同步复验，30 秒 TCP 是操作步骤。

UE 完成注册、IPv4 bearer、WWAN 和 ping 后，Spark **iperf 终端**启动服务：

```bash
iperf3 -s -B 10.45.0.1
```

UE laptop 开始 30 秒 TCP 流量后，Spark **xApp 终端**立即完整执行第 6 步，让约 10 秒的采集窗口覆盖正在传输的真实 UE。退出后，**留在同一个 xApp 终端**查看测量证据：

```bash
if [ -n "${KPM_LOG:-}" ] && [ -f "$KPM_LOG" ]; then
  grep -E 'UE ID type|^DRB\.|^RRU\.' "$KPM_LOG" | tail -n 20
else
  printf 'KPM 日志未设置或不存在；先在这个终端完整执行第 6 步。\n'
fi
```

若换了终端，先用 `read -r -p '粘贴第 6 步 log= 后的完整文件路径：' KPM_LOG` 指定那次采集的日志，再执行上面的检查；不要猜测历史日志文件名。

需看到 UE ID 和五项 DU 测量值再记录结果；空输出、`noValue` 或订阅成功本身不能证明已采集真实 UE 数据。

未安装时间戳补丁的 xApp 会显示无效的 `KPM-v3 ind_msg latency`；正常测量摘要不包含该行。当前源码已修复，构建、测试和安装验证通过；真实 UE 的时间显示复验见下一步。

### 7a 时间戳修复安装后复验

当前已确认安装的 KPM 库和 `xapp_oran_moni` 与修复构建一致。**CU、DU、RIC 不用重启**；等已有 xApp 退出后，按以下顺序复验。

1. Spark **iperf 终端**启动服务，已有服务则跳过：

```bash
iperf3 -s -B 10.45.0.1
```

2. UE laptop 在此前配置 WWAN 的同一个 Bash 终端启动 30 秒上行流量：

```bash
iperf3 -c 10.45.0.1 -B "${UE_IP:?请使用已配置WWAN的终端}" -t 30 -i 0
```

3. UE 流量启动后，立即在 Spark **xApp 终端**完整执行下面代码块，让约 10 秒的采集窗口覆盖流量。该块创建新的日志，不依赖旧终端中的 `KPM_LOG`：

```bash
KPM_LOG=$(mktemp /tmp/flexric-kpm.XXXXXX.log)
if /usr/local/bin/flexric/xApp/c/xapp_oran_moni \
  -c /usr/local/etc/flexric/xapp_oran_sm.conf >"$KPM_LOG" 2>&1; then
  grep -E 'report_age_us|UE ID type|Test xApp run' "$KPM_LOG" | tail -n 15
else
  tail -n 30 "$KPM_LOG"
fi
printf 'log=%s\n' "$KPM_LOG"
```

要求同时看到 `KPM-v3 report_age_us`、`UE ID type` 和 `Test xApp run SUCCESSFULLY`。只有成功退出而没有 UE ID / report age，说明本次没有收到 UE 报告，先确认 UE 流量与采集窗口重叠。完整日志保留在打印的路径；五项测量仍按第 7 步在同一终端查看。

`report_age_us` 单位是微秒，包含报告处理、排队和转发时间；不等于纯网络时延或应用 RTT。当前代码/离线验证及安装验证已通过，真实 UE 数值待复验。详细原因和精度限制见[时间戳修复记录](troubleshooting/2026-10-07_flexric_latency_fix.md)。

以后需要重新安装修复时，在 Spark 终端执行下列命令并输入 sudo 密码。脚本先备份再原子替换 KPM 库和两个 C monitor：

```bash
sudo bash /home/nyu/Desktop/Falcon_repo/Falcon/scripts/flexric/install_timestamp_fix.sh
```

## 8 关闭顺序

1. 两端停止 iperf / 视频等应用；让短时 xApp 完成正常删除并退出。
2. UE laptop 断开 bearer，清理本次手动 WWAN 地址和私网 host route，见[UE 关闭步骤](ue_laptop_quick_commands.md)。
3. **DU 终端 `Ctrl+C`**，等 `Stopping...` 并回到 shell；确认 DU 已退出后再停 FlexRIC 和 CU。
4. **FlexRIC 终端 `Ctrl+C`**，再在 **CU 终端 `Ctrl+C`**。
5. 检查终端按反序停止十个核心网服务；WebUI/MongoDB 可保留。

```bash
CORE_UNITS=(open5gs-nrfd open5gs-scpd open5gs-udrd open5gs-udmd open5gs-ausfd open5gs-pcfd open5gs-nssfd open5gs-upfd open5gs-smfd open5gs-amfd)
for unit in open5gs-amfd open5gs-smfd open5gs-upfd open5gs-nssfd open5gs-pcfd open5gs-ausfd open5gs-udmd open5gs-udrd open5gs-scpd open5gs-nrfd; do
  sudo systemctl stop "$unit" || break
done
remaining=0
if pgrep -a -x 'odu|nearRT-RIC|ocu|xapp_oran_moni'; then
  printf 'FAIL: 以上进程仍在运行\n'
  remaining=1
fi
for unit in "${CORE_UNITS[@]}"; do
  if systemctl is-active --quiet "$unit"; then
    printf 'FAIL: %s 仍 active\n' "$unit"
    remaining=1
  fi
done
[ "$remaining" -ne 0 ] || printf 'Stopped: DU / RIC / CU / xApp / 10 core units\n'
```

若需要完全停机，最后执行 `sudo docker stop open5gs-mongo`，以及已安装的 `sudo systemctl stop open5gs-webui`；不要删除容器或 volume。仅失败时展开当前日志，unit 换成失败的服务：

```bash
sudo journalctl -u open5gs-amfd --since '5 minutes ago' --no-pager -n 30
sudo grep -Ei 'E2|subscription|decode|error|failure' /tmp/du.log | tail -n 30
tail -n 30 /tmp/flexric-ric.log
```

RF 错误和 USB 问题按[完整流程](full_reproduction_runbook.md#9-confirm-rf-and-start-the-du)检查；运行中不要 probe B210。旧 FlexRIC `1a3903a7` 的本次 Format 4 订阅失败，使用已验证的 `73650812` 及配套库。
