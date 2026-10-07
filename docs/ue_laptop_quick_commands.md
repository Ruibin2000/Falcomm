# UE laptop 快速指令册

更新：2026-10-07。在 **UE laptop 的同一个 Bash 终端**按顺序执行，保留下面的变量；每步成功再继续。完整说明见 [复现手册第 10–14 节](full_reproduction_runbook.md#10-prepare-a-new-ubuntulinux-laptop)，Spark 启动见 [Spark 快速 CMD](spark_quick_commands.md)。

2026-10-05 已验证单 UE 注册、IPv4 和初次 10 秒 TCP 上行；2026-10-07 已验证 DU 的 FlexRIC KPM 订阅，并收到 gNB-DU UE ID `14` 的五项真实测量值，xApp 正常退出。以下 ping、30 秒上/下行是后续验收指令，不代表本次已测结果。

## 1 首次安装与选择 modem

新 laptop 首次安装；已装可跳过：

```bash
sudo apt update
sudo apt install -y modemmanager libqmi-utils minicom iproute2 iperf3 usbutils
sudo systemctl enable --now ModemManager
```

RMU500EK 接 RM500Q-GL、测试 USIM、天线、数据 USB 和辅助电源。已验证 firmware：`RM500QGLABR13A03M4G`。端口缺失见 [驱动检查](software_installation_and_drivers.md#72-kernel-driver-responsibilities)。

定义取值和选择函数一次；存在多个 RM500Q 时必须自己选择：

```bash
mm_value() {
  awk -F ':' -v key="$1" '
    { field=$1; gsub(/^[ \t]+|[ \t]+$/, "", field) }
    field==key { sub(/^[^:]*:[ \t]*/, ""); sub(/[ \t]+$/, ""); print; exit }
  '
}

select_rm500q() {
  MODEM=""
  local listing model
  local -a candidates
  listing=$(mmcli -L) || return 1
  mapfile -t candidates < <(printf '%s\n' "$listing" |
    awk '/RM500Q/ { print }' | grep -oE '/org/freedesktop/ModemManager1/Modem/[0-9]+')
  case ${#candidates[@]} in
    0) printf '未发现 RM500Q；检查 USB/驱动后重试。\n'; return 1 ;;
    1) MODEM=${candidates[0]} ;;
    *) printf '%s\n' "$listing"; read -r -p '输入目标 RM500Q 的完整 Modem 路径：' MODEM ;;
  esac
  MODEM_KV=$(mmcli -m "$MODEM" -K) || { MODEM=""; return 1; }
  model=$(printf '%s\n' "$MODEM_KV" | mm_value modem.generic.model)
  MODEM_IDENTITY=$(printf '%s\n' "$MODEM_KV" | mm_value modem.generic.device-identifier)
  if [[ "$model" != *RM500Q* ]] || [[ -z "$MODEM_IDENTITY" || "$MODEM_IDENTITY" = -- ]]; then
    MODEM=""; printf '型号或设备标识不匹配，停止。\n'; return 1
  fi
  printf 'modem=%s model=%s firmware=%s\n' "$MODEM" "$model" \
    "$(printf '%s\n' "$MODEM_KV" | mm_value modem.generic.revision)"
}
select_rm500q
```

若 modem 为 disabled：`sudo mmcli -m "$MODEM" --enable`。不要沿用历史 modem/SIM/bearer 编号。

## 2 NR SA 与私网小区锁定

已有正确设置且已注册时，直接到第 3 步。需要调整时，选择 **实际 `(at)` 端口**：

```bash
AT_PORT=$(mmcli -m "$MODEM" | grep -oE 'ttyUSB[0-9]+ \(at\)' | head -1 | awk '{print $1}')
if [ -n "$AT_PORT" ]; then
  sudo minicom -D "/dev/$AT_PORT" -b 115200
else
  printf '未找到 AT 端口，停止并检查 modem ports。\n'
fi
```

在 minicom 中输入，**不是 Bash 命令**；每次写入都必须返回 `OK`：

```text
AT
AT+QNWPREFCFG="mode_pref",NR5G
AT+QNWPREFCFG="mode_pref"
AT+QNWPREFCFG="nr5g_disable_mode",2
AT+QNWPREFCFG="nr5g_disable_mode"
AT+QNWPREFCFG="nr5g_band"
AT+QNWLOCK="common/5g",1,649632,30,78
AT+QNWLOCK="common/5g"
AT+QENG="servingcell"
AT+C5GREG?
```

确认：band 含 78；锁定 `PCI=1 / SSB=649632 / SCS=30 / n78`；serving cell 为 `NR5G-SA / 001,01 / TAC 7`；注册返回 `+C5GREG: 0,1`。`NOCONN` 可表示驻留空闲，以 C5GREG 注册结果为准。**锁定用 SSB 649632，不能换成载波 650000。**

该 firmware 的 disable mode `2` 禁用 NSA。退出：`Ctrl+A`，再 `X`。关闭重复 AT 会话，保留 ModemManager。

可选重启仅在确有需要时用 `AT+CFUN=1,1`；USB 重枚举后重新执行 `select_rm500q`、发现 AT 端口，再设置/检查上述偏好和锁定。**最终锁定后不再重启 modem。**

## 3 注册确认与 IPv4 连接

简短注册确认：

```bash
mmcli -m "$MODEM" -K | awk -F ':' '
  /modem\.generic\.(state|access-technologies)(\.value\[[0-9]+\])?[[:space:]]*:/ ||
  /modem\.3gpp\.(operator-code|registration-state|packet-service-state)[[:space:]]*:/ {print}
'
```

要求 `5gnr / 00101 / home / attached`，state 为 registered 或 connected。随后建立 `internet` **IPv4** PDU：

```bash
sudo mmcli -m "$MODEM" --simple-connect="apn=internet,ip-type=ipv4"
```

若失败停止，见 [PDU/IPv4v6 故障记录](troubleshooting/2026-10-05_sa_bringup.md#10-pdu-root-cause-ipv4v6-rejected-by-ocudu)。Spark subscriber 必须为 session type `1`；无需修改 EPS initial bearer 显示的 IPv4v6。

## 4 找实际 bearer 和配置专用 WWAN

只从选中 modem 找 **connected + internet** bearer，不使用 EPS initial bearer。多个匹配项时停止，不猜编号：

```bash
BEARER=""
matches=()
discovery_ok=yes
if MODEM_KV=$(mmcli -m "$MODEM" -K); then
  for path in $(printf '%s\n' "$MODEM_KV" |
    grep -oE '/org/freedesktop/ModemManager1/Bearer/[0-9]+' | sort -u); do
    details=$(mmcli -b "$path" -K) || { discovery_ok=no; break; }
    connected=$(printf '%s\n' "$details" | mm_value bearer.status.connected)
    apn=$(printf '%s\n' "$details" | mm_value bearer.properties.apn)
    if [ "$connected" = yes ] && [ "$apn" = internet ]; then matches+=("$path"); fi
  done
else
  discovery_ok=no
fi
if [ "$discovery_ok" = yes ] && [ ${#matches[@]} -eq 1 ] && \
   BEARER_KV=$(mmcli -b "${matches[0]}" -K); then
  BEARER=${matches[0]}
  UE_CONNECTED=$(printf '%s\n' "$BEARER_KV" | mm_value bearer.status.connected)
  UE_SUSPENDED=$(printf '%s\n' "$BEARER_KV" | mm_value bearer.status.suspended)
  WWAN_IF=$(printf '%s\n' "$BEARER_KV" | mm_value bearer.status.interface)
  UE_IP=$(printf '%s\n' "$BEARER_KV" | mm_value bearer.ipv4-config.address)
  UE_PREFIX=$(printf '%s\n' "$BEARER_KV" | mm_value bearer.ipv4-config.prefix)
  UE_MTU=$(printf '%s\n' "$BEARER_KV" | mm_value bearer.ipv4-config.mtu)
  UE_METHOD=$(printf '%s\n' "$BEARER_KV" | mm_value bearer.ipv4-config.method)
  printf 'bearer=%s connected=%s suspended=%s if=%s IPv4=%s/%s mtu=%s method=%s\n' \
    "$BEARER" "$UE_CONNECTED" "$UE_SUSPENDED" "$WWAN_IF" "$UE_IP" "$UE_PREFIX" "$UE_MTU" "$UE_METHOD"
else
  printf '未取得唯一可用 bearer（发现阶段=%s，匹配数=%s）；停止并检查。\n' "$discovery_ok" "${#matches[@]}"
fi
```

要求 `connected=yes / suspended=no / static`。历史值是 `wwan0 / 10.45.0.2/30 / MTU 1400`；实际配置使用刚读取的值。ModemManager 提供 IP 参数，主机仍需设置接口。

以下只用于专用 QMI WWAN。已有 NetworkManager cellular profile 时，先处理它的 WWAN 配置所有权；保留管理 Wi-Fi 与 NetworkManager。默认路由、SSH 管理路径或其他 IPv4 在该接口时，脚本拒绝改动。

```bash
configure_wwan() {
  local current_ips mgmt_dev driver
  [[ -n "$BEARER" && "$UE_CONNECTED" = yes && "$UE_SUSPENDED" = no && "$UE_METHOD" = static ]] || return 1
  [[ "$UE_IP" =~ ^10\.45\.([0-9]{1,3})\.([0-9]{1,3})$ ]] || return 1
  (( 10#${BASH_REMATCH[1]} <= 255 && 10#${BASH_REMATCH[2]} <= 255 )) || return 1
  [[ "$UE_IP" != 10.45.0.1 && "$UE_PREFIX" =~ ^[0-9]+$ && "$UE_MTU" =~ ^[0-9]+$ ]] || return 1
  (( UE_PREFIX >= 1 && UE_PREFIX <= 32 && UE_MTU >= 576 && UE_MTU <= 65535 )) || return 1
  [[ "$WWAN_IF" =~ ^(wwan|wwp|rmnet)[[:alnum:]_.-]*$ ]] || return 1
  ip link show dev "$WWAN_IF" >/dev/null || return 1
  driver=$(basename "$(readlink -f "/sys/class/net/$WWAN_IF/device/driver")")
  [[ "$driver" = qmi_wwan ]] || return 1
  printf '%s\n' "$MODEM_KV" | grep -Fq "$WWAN_IF (net)" || return 1
  [[ -z "$(ip -4 route show default dev "$WWAN_IF")" &&
     -z "$(ip -6 route show default dev "$WWAN_IF")" ]] || return 1
  if [ -n "$SSH_CONNECTION" ]; then
    mgmt_dev=$(ip route get "${SSH_CONNECTION%% *}" | awk '{for(i=1;i<NF;i++) if($i=="dev") print $(i+1)}')
    [[ -n "$mgmt_dev" && "$mgmt_dev" != "$WWAN_IF" ]] || return 1
  fi
  current_ips=$(ip -o -4 addr show dev "$WWAN_IF" | awk '{print $4}')
  [[ -z "$current_ips" || "$current_ips" = "$UE_IP/$UE_PREFIX" ]] || return 1
  sudo ip link set dev "$WWAN_IF" mtu "$UE_MTU" up &&
  sudo ip -4 addr replace "$UE_IP/$UE_PREFIX" dev "$WWAN_IF" noprefixroute &&
  sudo ip -4 route replace 10.45.0.1/32 dev "$WWAN_IF" src "$UE_IP"
}
if configure_wwan; then
  ip -4 route get 10.45.0.1 from "$UE_IP"
  ip -4 route show default
else
  printf 'WWAN 配置未完成；检查 bearer、专用接口与管理路由，不要继续测试。\n'
fi
```

要求到 `10.45.0.1` 的 route 使用 `$WWAN_IF` 和 `$UE_IP`；Wi-Fi 默认路由保持。这里只新增私网 host route，不加默认路由、NAT 或 MASQUERADE。

## 5 Ping 流量与 KPM

Ping 仅保留汇总，失败仍可见：

```bash
ping -q -I "$WWAN_IF" -c 5 10.45.0.1
```

要求 `0% packet loss`。将一行 bearer 中的实际 `UE_IP` 给 Spark 端做 reverse ping；Spark 先启动 `iperf3 -s -B 10.45.0.1`。

以下 **逐条运行**，末尾保留 sender/receiver 汇总：

```bash
# 上行：UE → Spark，30 秒（待验证）
iperf3 -c 10.45.0.1 -B "${UE_IP:?请使用已配置WWAN的终端}" -t 30 -i 0

# 下行：Spark → UE，30 秒（待验证）
iperf3 -c 10.45.0.1 -B "${UE_IP:?请使用已配置WWAN的终端}" -R -t 30 -i 0
```

时间戳修复后的 KPM 库和 monitor 已安装，并校验与新构建一致。保持 Spark 的 **CU、DU、FlexRIC** 运行，已有 iperf server 可继续使用。在已配置 WWAN 的 UE 终端开始上述 30 秒上行，**Spark 随即运行新安装的约 10 秒 `xapp_oran_moni`**，见 [Spark 安装后复验](spark_quick_commands.md#7a-时间戳修复安装后复验)。要求出现 `report_age_us`、UE ID 与 KPM 值，并正常退出。

2026-10-07 已收到 UE ID `14` 的五项 DU 测量值；仅 `Successfully subscribed` 不能证明收到数据。时间戳修复后的真实 UE `report_age_us` 和本次 30 秒 iperf 结果仍待复验，也没有新的 ping 或长时稳定性证据；两端记录方向、汇总速率、重传和 Spark RF 错误增长。

SIM/注册问题可用实际 `(qmi)` 端口做 `qmicli -p` 查询，见 [完整手册第 10 节](full_reproduction_runbook.md#10-prepare-a-new-ubuntulinux-laptop)；不要与 ModemManager 抢占无 proxy 的 QMI 会话。

## 6 UE 停机

先 `Ctrl+C` 停止流量。使用同一终端保存的变量，核对同一 modem 后 **先断开 PDU，再只删除本次手工 IP 与 host route**。不要重选首个 modem 或 flush 全接口。

```bash
cleanup_ue() {
  local current_identity current_modem_kv driver
  [[ -n "$MODEM" && -n "$MODEM_IDENTITY" && -n "$WWAN_IF" && -n "$UE_IP" && -n "$UE_PREFIX" ]] || return 1
  current_modem_kv=$(mmcli -m "$MODEM" -K) || return 1
  current_identity=$(printf '%s\n' "$current_modem_kv" | mm_value modem.generic.device-identifier)
  [[ "$current_identity" = "$MODEM_IDENTITY" ]] || return 1
  printf '%s\n' "$current_modem_kv" | grep -Fq "$WWAN_IF (net)" || return 1
  sudo mmcli -m "$MODEM" --simple-disconnect || return 1
  driver=$(basename "$(readlink -f "/sys/class/net/$WWAN_IF/device/driver")")
  [[ "$driver" = qmi_wwan ]] || return 1
  if ip -4 route show exact 10.45.0.1/32 dev "$WWAN_IF" | grep -Fq "src $UE_IP"; then
    sudo ip -4 route del 10.45.0.1/32 dev "$WWAN_IF" src "$UE_IP" || return 1
  fi
  if ip -o -4 addr show dev "$WWAN_IF" | awk '{print $4}' | grep -Fxq "$UE_IP/$UE_PREFIX"; then
    sudo ip -4 addr del "$UE_IP/$UE_PREFIX" dev "$WWAN_IF" || return 1
  fi
  printf 'PDU 已断开，本次手工地址和路由已清理。\n'
  ip -br -4 addr show dev "$WWAN_IF"
}
cleanup_ue || printf '清理未完成；检查同一 modem/接口及命令错误，不要猜接口或执行 flush。\n'
```

变量丢失或 modem 已重启时，先重新确认原设备、接口和本次地址再清理，不能直接执行旧变量命令。可选在断开与清理完成后执行 `sudo mmcli -m "$MODEM" --disable`，随后再拔 RMU。Spark 按 [Spark 快速 CMD](spark_quick_commands.md) 停机。
