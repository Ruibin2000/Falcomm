# Development Handoff

Updated: 2026-10-05. Continue from the working **single-link 5G SA IPv4 baseline**. Read [Progress and Configuration](project_progress_and_configuration.md), the [Full Reproduction Runbook](full_reproduction_runbook.md), and the [2026-10-05 Debug Record](troubleshooting/2026-10-05_sa_bringup.md) before proposing changes.

## Achieved and pending

The reported experiment established private n78 detection, PRACH/PUSCH CRC OK, SA registration/authentication, packet attachment, IPv4 PDU setup, a connected static bearer, manually configured WWAN, and laptop-to-Spark application traffic. The first 10-second TCP uplink was sender 6.29 Mbit/s / receiver 5.09 Mbit/s, sender Retr 0. Sustained throughput, downlink, UDP and live video are not yet validated.

The immediate task is cold-start/new-laptop reproducibility and characterization. Long-term dual FR1/FR3 radio paths, FlexRIC telemetry, xApps, AI channel/rate prediction and MPTCP video steering remain the target in [README](../README.md#target-architecture). Do not activate those components until the single-link baseline has been reproduced and frozen.

## Stable infrastructure facts

- Spark `spark-7c40`: Ubuntu 24.04.5, aarch64, 20 cores, 121 GiB, NVIDIA GB10; kernel `7.0.0-1019-nvidia`, PREEMPT_DYNAMIC.
- OCUDU `/home/nyu/ocudu`, `release_26_04` / `050a2bb`, Clang 18.1.3. CU executable `build/apps/cu/ocu`, DU `build/apps/du_split_8/odu`.
- Configuration paths: `/home/nyu/ocudu/configs/cu.yml` and `du1_b210_n78_20mhz.yml`; Open5GS `/etc/open5gs/{nrf,amf,smf,upf}.yaml`.
- Open5GS `2.8.0~noble5`; MongoDB 7 Docker container `open5gs-mongo`, image `mongo:7.0-jammy`, volume `open5gs-mongo-data`; native MongoDB 8 disabled.
- B210 `3271233` / UHD 4.6.0.0 / USB 3; n78, carrier ARFCN 650000 / 3750 MHz, SSB ARFCN 649632 / 3744.48 MHz, 20 MHz, 30 kHz, PCI 1, SISO.
- Verified gain **TX 80 / RX 40**, sample rate 23.04 MS/s, internal clock, clock_ppm 0, freq_offset 0; CU/DU INFO logs.
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

Private QSCAN and PUSCH CRC OK at 20–33 dB SINR established usable downlink acquisition and uplink with RX gain 40. The unsupported IMS request was not the `internet` failure. Do not reopen these diagnoses without contradictory new evidence.

## Working procedures

1. Check current processes/services; historical verification does not imply live components are running.
2. During interactive bring-up, give one next step with exact host label, expected evidence and success/failure layer. Use real command output before advancing.
3. Use the canonical runbook service set/order: NRF, SCP, UDR, UDM, AUSF, PCF, NSSF, UPF, SMF, AMF. BSF/SEPP are optional, EPC services are legacy, WebUI is administration.
4. Preserve the NVIDIA kernel, compiler defaults, boot parameters and current working RAN configuration. No PREEMPT_RT, CPU isolation, IRQ or hugepage changes without a specific evidenced need.
5. The recorded performance script already sets 20 performance governors, disables KMS polling and configures four network-buffer values at 33554432. Check after reboot. CU/DU launch with sudo; recurring RF failures remain a monitoring issue.
6. Check band, carrier/SSB, gain, serial and authorized lab conditions before RF startup. Never probe/benchmark B210 while DU owns it. Gain values are not calibrated dBm.
7. Never infer Ki/OP/OPc from IMSI; keep credentials, authentication vectors and derived RAN keys out of Git. Sanitize INFO logs.
8. Dynamically discover modem, SIM, bearer, QMI/AT port, WWAN interface and allocated UE address. QMI uses proxy. Query the active bearer rather than EPS initial-bearer display.
9. Keep laptop Wi-Fi for management. Local Spark application traffic requires neither Internet NAT nor a laptop default-route change.
10. Record tests by direction, duration, rate, retransmission/jitter/loss and concurrent RF-error growth. Do not present first TCP success as sustained performance.
11. Shutdown applications, PDU and laptop configuration first, then DU, CU and core. Keep MongoDB data; optional/legacy services need explicit cleanup for full shutdown.
12. Documentation maintenance does not authorize starting services, RF, benchmarking, committing or pushing. The operator requested no automatic push/merge for this update.

## Next session

Cold-start/new-laptop reproduction → 30–60-second TCP uplink → TCP downlink → UDP 2/4/6/8/10 Mbit/s → jitter/loss and DU-error observation → camera/video and latency/QoE → freeze Single-Link Baseline v1 → second independent radio path → independent verification of both → MPTCP → FlexRIC telemetry → xApp → AI predictor → active path steering → blockage/QoE comparison.

Use the [Full Reproduction Runbook](full_reproduction_runbook.md) for operating commands and [Software Installation, Drivers, and OCUDU Environment](software_installation_and_drivers.md) for dependencies, driver inspection, compiler/build settings, and installation issues.
