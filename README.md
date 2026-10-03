# Falcomm

Falcomm 是一个基于 NVIDIA DGX Spark 的 5G / AI-RAN 实验平台。项目目标是在同一台设备上集成 OCUDU、Open5GS、FlexRIC 和 AI 控制逻辑，并通过多条 5G 链路研究网络测量、路径选择与 MPTCP 流量调度。

## 目标架构

```text
USRP B210 #1 ─ DU1 ─┐
                     ├─ OCUDU CU ─ Open5GS 5GC ─ MPTCP Server
USRP B210 #2 ─ DU2 ─┘       │
                             └─ FlexRIC ─ xApp ─ AI 策略控制器
```

预期控制流程：DU 通过 E2 向 FlexRIC 提供测量数据，xApp/AI 控制器据此调整 Linux MPTCP 路径或调度策略。

## 当前进度

截至 **2026-10-02**，已完成 DGX Spark 基线、UHD/B210 #1 验证、OCUDU 编译、单 CU + 单 DU 与 Open5GS 基础连接。N2（CU 到 AMF）和 F1（CU 到 DU）已建立，DU 已通过 UHD 驱动 B210 #1。

当前拓扑可概括为：

```text
MongoDB 7 (Docker) → Open5GS → OCUDU CU → OCUDU DU1 → UHD → USRP B210 #1
```

UE subscriber/attach、第二个 DU/B210、实时调优、FlexRIC/xApp、MPTCP 双路径和 AI 主动选路仍在后续计划中。详细阶段状态见[项目进展](01_project_progress.md)。

## 平台与版本

| 组件 | 当前记录 |
|---|---|
| 主机 | NVIDIA DGX Spark，aarch64，Ubuntu 24.04.5 LTS |
| Kernel | NVIDIA `7.0.0-1019-nvidia`，`PREEMPT_DYNAMIC` |
| OCUDU | `release_26_04`，commit `050a2bb`，使用 Clang 18 |
| Open5GS | `2.8.0~noble5` |
| MongoDB | Docker `mongo:7.0-jammy`（arm64） |
| 射频接口 | Ettus B210 #1，UHD `4.6.0.0`；当前部署不使用 ZeroMQ |
| MPTCP | 当前 NVIDIA kernel 原生支持，`net.mptcp.enabled = 1` |

上述版本与运行结果来自项目部署记录。FlexRIC、xApp 和 AI 控制链尚未部署完成。

## 文档导航

- [项目进展与系统配置](01_project_progress.md)：目标架构、阶段状态、组件版本、CU/DU 和 Open5GS 配置记录。
- [日常操作手册](02_operation_commands.md)：服务启动/停止、CU/DU 启动、链路检查和常见操作。
- [复现安装步骤](03_reproducible_installation_steps.md)：从干净 DGX Spark 环境复现当前已完成部署的步骤。
- [后续工作交接说明](04_codex_handoff_prompt.md)：当前环境约束、已知状态和继续开发时的工作原则。

## 快速操作

日常启动顺序及完整检查命令见[操作手册](02_operation_commands.md)。核心顺序如下：

1. 启动 MongoDB Docker 容器和 Open5GS 服务。
2. 在 `~/ocudu` 启动 CU：`build/apps/cu/ocu -c configs/cu.yml`。
3. 确认 B210 可用，再启动 DU：`build/apps/du_split_8/odu -c configs/du1_b210_n78_20mhz.yml`。
4. 检查 AMF 的 N2、CU/DU 的 F1-C 连接及 DU/UHD 日志。

操作前请确认本地配置文件和设备状态与[操作手册](02_operation_commands.md)一致。当前 DU 配置会在约 **3.75 GHz** 发射 RF；仅应在获准的频段、地点和功率条件下运行。DU 正在使用 B210 时，不要同时运行会打开该设备的 UHD probe 或 benchmark。

Falcomm 当前实机链路使用 **UHD + USRP B210**。官网 RIC 教程中的 ZeroMQ 配置是软件射频示例，不属于本项目当前部署；后续验证 FlexRIC 时应在现有 UHD/DU 配置上启用 E2。

## 当前边界

- 已验证的是单 DU、单 B210 的基础链路，不代表 UE 已完成注册或端到端数据业务已验证。
- 30.72 MS/s、20 秒的 UHD benchmark 是射频设备/USB 压力测试；OCUDU 当前配置采样率为 23.04 MS/s。
- 暂不更换 NVIDIA kernel、不安装 PREEMPT_RT，也不更改系统默认 GCC；这些约束有助于保持当前可复现环境。
