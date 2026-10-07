# Development Handoff

Updated: 2026-10-07. Continue from the working **single-link 5G SA IPv4 baseline with actual UE KPM reception through FlexRIC verified**. Read [Progress and Configuration](project_progress_and_configuration.md), the [Full Reproduction Runbook](full_reproduction_runbook.md), and the [FlexRIC record](troubleshooting/2026-10-07_flexric_bringup.md). Daily commands are summarized separately for [Spark](spark_quick_commands.md) and the [UE laptop](ue_laptop_quick_commands.md).

## Achieved and pending

The reported experiment established private n78 detection, PRACH/PUSCH CRC OK, SA registration/authentication, packet attachment, IPv4 PDU setup, a connected static bearer, manually configured WWAN, and laptop-to-Spark application traffic. The first 10-second TCP uplink was sender 6.29 Mbit/s / receiver 5.09 Mbit/s, sender Retr 0. Sustained throughput, downlink, UDP and live video are not yet validated.

The 2026-10-07 session added FlexRIC, verified DU E2 Setup, and completed the C KPM xApp subscription and deletion. A later run received all five actual DU metrics for UE F1AP ID `14`: selected UL throughput samples were 14.163–14.675 Mbit/s at the RLC measurement layer, with UL PRB usage 86%. Next acceptance work is synchronized KPM/application traffic correlation, cold-start/new-laptop reproducibility and traffic characterization. Dual FR1/FR3 paths, AI prediction and MPTCP steering remain future work; freeze the single-link baseline before adding a second path or active steering.

## Stable infrastructure facts

- Spark `spark-7c40`: Ubuntu 24.04.5, aarch64, 20 cores, 121 GiB, NVIDIA GB10; kernel `7.0.0-1019-nvidia`, PREEMPT_DYNAMIC.
- OCUDU `/home/nyu/ocudu`, `release_26_04` / `050a2bb`, Clang 18.1.3. CU executable `build/apps/cu/ocu`, DU `build/apps/du_split_8/odu`.
- FlexRIC `/home/nyu/flexric`, pinned `736508123fe4b5dc3db83fb5baf5f0a8e9b04fe8`, GCC 13.3 Debug, `build-ocudu`, `E2AP_V3` / `KPM_V3_00`, `NONE_XAPP`, multilanguage OFF. Installed binaries/configs use `/usr/local/bin/flexric`, `/usr/local/etc/flexric`, `/usr/local/lib/flexric`.
- Configuration paths: `/home/nyu/ocudu/configs/cu.yml` and `du1_b210_n78_20mhz.yml`; Open5GS `/etc/open5gs/{nrf,amf,smf,upf}.yaml`.
- DU E2 overlay `configs/du1_flexric.yml` is loaded after the UHD base file. It enables DU E2, KPM, RLC/scheduler metrics and a 1000 ms DU report period. FlexRIC SCTP ports are E2 `36421`, xApp E42 `36422`, both `127.0.0.1`.
- Open5GS `2.8.0~noble5`; MongoDB 7 Docker container `open5gs-mongo`, image `mongo:7.0-jammy`, volume `open5gs-mongo-data`; native MongoDB 8 disabled.
- B210 `3271233` / UHD 4.6.0.0 / USB 3; n78, carrier ARFCN 650000 / 3750 MHz, SSB ARFCN 649632 / 3744.48 MHz, 20 MHz, 30 kHz, PCI 1, SISO.
- The 2026-10-05 UE traffic baseline used **TX 80 / RX 40**; the 2026-10-07 actual DU file has **TX 70 / RX 40**. Preserve the actual gain and record it per test. Sample rate 23.04 MS/s, internal clock, clock_ppm 0, freq_offset 0; CU/DU INFO logs.
- Private identity PLMN `00101`, TAC 7, SST 1, DNN `internet`, test IMSI `001010000000001`, subscriber session type 1 / IPv4 only.
- UE RMU500EK / RM500QGL_VH, firmware `RM500QGLABR13A03M4G`. NR5G-only and disable-mode 2 (NSA disabled on this firmware), lock PCI 1 / SSB 649632 / SCS 30 / n78.
- Addresses: CU N2/N3 `127.0.0.1`; AMF `127.0.0.5`; CU F1 `127.0.10.1`; DU F1 `127.0.10.2`; UPF `127.0.0.7`; SMF `127.0.0.4`; Spark local private gateway `10.45.0.1` on ogstun.

The current CU has an explicit `ngu` bind; it was not proven necessary as a root-cause repair. The latest DU has no explicit old `otw_format: sc12` entry. Preserve the actual configuration and inspect runtime sockets before changing either.

## Resolved failures and their evidence

| Failure | Resolved cause |
|---|---|
| Reject 95 / NRF discovery 500 / AMF HTTP 400 | NRF serving PLMN `999/70` corrected to `001/01` |
| Reject 7 / UDR 404 / missing SUPI/IMSI | Exact SIM subscriber absent and then provisioned |
| Public-network drift / lock state errors | NR/SA selection followed by private SSB lock; settings rechecked after reboot |
| Connected registration but CallFailed / CU rejects IPv4v6 | Subscriber type 3 → type 1; IPv4 simple-connect |
| Recurring RF underflow/late | OCUDU host performance script Y/Y/Y and privileged RAN launch |
| Static bearer connected but no WWAN IP | Actual bearer IP/prefix/MTU applied to laptop interface |
| KPM Format 4 subscription fails, RIC asserts, xApp times out | `1a3903a7` modified ASN.1 codec replaced by standard codec in `73650812`; subscription/deletion then passed |

Private QSCAN and PUSCH CRC OK at 20–33 dB SINR established usable downlink acquisition and uplink with RX gain 40. The unsupported IMS request was not the `internet` failure. Do not reopen these diagnoses without contradictory new evidence.

## Working procedures

1. Check current processes/services; historical verification does not imply live components are running.
2. During interactive bring-up, give one next step with exact host label, expected evidence and success/failure layer. Use real command output before advancing.
3. Use the canonical runbook service set/order: NRF, SCP, UDR, UDM, AUSF, PCF, NSSF, UPF, SMF, AMF. BSF/SEPP are optional, EPC services are legacy, WebUI is administration.
    Run CU and FlexRIC before DU. DU E2 Setup depends on F1 Setup component data; reconnecting after a RIC restart requires a DU restart. Start the xApp after RIC has registered the DU.
4. Preserve the NVIDIA kernel, compiler defaults, boot parameters and current working RAN configuration. No PREEMPT_RT, CPU isolation, IRQ or hugepage changes without a specific evidenced need.
5. The recorded performance script already sets 20 performance governors, disables KMS polling and configures four network-buffer values at 33554432. Check after reboot. CU/DU launch with sudo; recurring RF failures remain a monitoring issue.
6. Check band, carrier/SSB, gain, serial and authorized lab conditions before RF startup. Never probe/benchmark B210 while DU owns it. Gain values are not calibrated dBm.
7. Never infer Ki/OP/OPc from IMSI; keep credentials, authentication vectors and derived RAN keys out of Git. Sanitize INFO logs.
8. Dynamically discover modem, SIM, bearer, QMI/AT port, WWAN interface and allocated UE address. QMI uses proxy. Query the active bearer rather than EPS initial-bearer display.
9. Keep laptop Wi-Fi for management. Local Spark application traffic requires neither Internet NAT nor a laptop default-route change.
10. Record tests by direction, duration, rate, retransmission/jitter/loss and concurrent RF-error growth. Do not present first TCP success as sustained performance.
11. Stop traffic and let the xApp delete its subscription, then release PDU/laptop configuration, stop DU, FlexRIC, CU and core. Keep MongoDB data; optional/legacy services need explicit cleanup for full shutdown.
12. Documentation maintenance does not authorize starting services, RF, benchmarking, committing or pushing. The operator requested no automatic push/merge for this update.

## Next session

Repeat the approximately 10-second C KPM xApp during a measured private traffic run, recording current UE ID, direction, application rate, duration and report timing together. Initial real UE metric reception is already verified. Continue with cold-start/new-laptop reproduction → 30–60-second TCP uplink → TCP downlink → UDP 2/4/6/8/10 Mbit/s → jitter/loss and DU-error observation → camera/video and latency/QoE → freeze Single-Link Baseline v1 → second path → MPTCP → AI predictor → active steering → blockage/QoE comparison.

The original example's huge negative `KPM ... ind_msg latency` is invalid. The [timestamp repair](troubleshooting/2026-10-07_flexric_latency_fix.md) is applied to local FlexRIC source, rebuilt, tested and installed; do not discard these local source changes when switching commits. It normalizes v3 ASN timestamps to Unix microseconds and prints signed `report_age_us`. Installed KPM library and `xapp_oran_moni` match the repaired build. An isolated subscription passed without UE indications; live UE report-age verification remains pending. Follow [Spark step 7a](spark_quick_commands.md#7a-时间戳修复安装后复验): keep CU/DU/RIC running, start UE traffic, then immediately run the new approximately 10-second xApp and require report age, actual UE ID and normal exit together. `DRB.RlcSduDelayDl` has its own 0.1 ms unit: 15.80–17.30 means 1.580–1.730 ms of RLC SDU delay.

Use the [Full Reproduction Runbook](full_reproduction_runbook.md) for operating commands and [Software Installation, Drivers, and OCUDU Environment](software_installation_and_drivers.md) for dependencies, driver inspection, compiler/build settings, and installation issues.
