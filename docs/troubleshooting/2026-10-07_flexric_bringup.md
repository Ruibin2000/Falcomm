# FlexRIC integration on Spark

Date: **2026-10-07**. OCUDU DU1 connected to FlexRIC over E2, and the C KPM xApp completed subscription, deletion and normal exit. **Actual UE KPM reports remain pending.** The 2026-10-05 UE traffic result remains the application-data baseline.

## Working configuration

| Component | Value |
|---|---|
| Host | `spark-7c40`, DGX Spark, arm64, Ubuntu 24.04.5 |
| OCUDU | `050a2bb`, existing CU/DU binaries, B210/UHD |
| FlexRIC | `br-flexric`, `736508123fe4b5dc3db83fb5baf5f0a8e9b04fe8` |
| Compiler and build | GCC 13.3, Debug, `build-ocudu` |
| Options | `E2AP_V3`, `KPM_V3_00`, `NONE_XAPP`, `XAPP_MULTILANGUAGE=OFF` |
| E2 / E42 listeners | `127.0.0.1:36421` / `127.0.0.1:36422`, SCTP |
| DU configs | `du1_b210_n78_20mhz.yml` followed by `du1_flexric.yml` |
| KPM | Function ID 2, Format 4, 1000 ms, five DU measurements |
| Radio | B210 `3271233`, USB 3, 23.04 MS/s, n78, 20 MHz, PCI 1, carrier 650000 / SSB 649632 |
| Current gain | Inspected TX 70 / RX 40; earlier 2026-10-05 UE traffic used TX 80 / RX 40 |

Use the [installation guide](../software_installation_and_drivers.md#8-flexric-installation-on-spark), [canonical runtime steps](../full_reproduction_runbook.md#9-confirm-rf-and-start-the-du), and [Spark](../spark_quick_commands.md) / [UE laptop](../ue_laptop_quick_commands.md) quick commands to reproduce.

## Initial installation and E2 Setup

The missing packages were `cmake-curses-gui`, `libconfig-dev` and `libconfig++-dev`; existing build tools, SCTP, SWIG and Python development files were present. FlexRIC was initially pinned to `1a3903a7`, configured with `E2AP_V3`, `KPM_V3`, `NONE_XAPP` and Python OFF, then built and installed. Both localhost SCTP ports listened successfully.

The DU overlay enabled `enable_du_e2`, KPM, RLC/scheduler metrics and a 1000 ms DU report period. The two-config `--dryrun` returned 0. This checks parsing only and does not start the radio. No OCUDU rebuild or ZeroMQ radio change was required.

MongoDB returned `{ ok: 1 }`; all ten required Open5GS services were active. CU connected to AMF and listened for F1. DU initialized B210 over USB 3, completed F1 and E2 connections and started. RIC registered PLMN `001/01`, node ID 411, `ngran_gNB_DU`, DU ID 1 and KPM RAN function ID 2. SCTP N2, F1 and E2 associations were established.

## Subscription failure and codec diagnosis

The first C xApp connected to RIC and discovered one DU, but its Format 4 subscription failed. The observed sequence was:

1. DU received the KPM subscription and failed Action Definition decoding, including `Invalid choice id=38 for choice type meas_type_c`.
2. DU returned Subscription Failure.
3. FlexRIC asserted in `e2ap_handle_subscription_failure_ric` with `Not implemented` and aborted.
4. The xApp then timed out waiting for its subscription response and aborted.

The old compiled KPM codec used modified ASN.1 extension boundaries. Source inspection found these differences from OCUDU's standard encoding:

| Field | Old FlexRIC `1a3903a7` | Standard codec in `73650812` |
|---|---|---|
| Action Definition Formats 4/5 | Root CHOICE alternatives | Extension alternatives |
| Test condition S-NSSAI | Root CHOICE alternative | Extension alternative |
| Format 1 distribution range | Root optional field | Extension optional field |
| Indication Message Format 3 | Root CHOICE alternative | Extension alternative |

The Format 4 selection was therefore decoded as the wrong format and corrupted later field interpretation. Disabling the RIC assertion would not correct this encoding mismatch. Absence of a UE did not cause the subscription rejection; an eligible UE is needed later to produce Format 4 reports.

## Version correction and offline verification

FlexRIC was changed to `736508123fe4b5dc3db83fb5baf5f0a8e9b04fe8`. Its generated codec uses standard KPM ASN.1. A separate `build-ocudu` directory used the new `KPM_V3_00` option, GCC 13.3 Debug and multilanguage OFF. The complete build and install succeeded. The built and installed KPM libraries had identical SHA-256 values:

```text
74f62b45b8ac37b770b20e53a39e0fa19eac5944ebb0b4a977e5a906b40f2711
```

This is the session's artifact hash; reproduction checks equality between its own built and installed files.

Offline cross-codec tests used synthetic messages:

| Test | Old `1a3903a7` | New `73650812` |
|---|---|---|
| Format 4 subscription encoded by FlexRIC, decoded by existing OCUDU | Fails; incorrectly selects Format 2 | Passes: Format 4, SST condition 1, 1000 ms, all five metric names and no-label labels |
| Format 1 subscription decoded by OCUDU | Passes | Passes |
| Event trigger decoded by OCUDU | Passes, 1000 ms | Passes, 1000 ms |
| OCUDU Format 3 indication decoded by FlexRIC | Not tested | Passes: synthetic gNB-DU UE ID 7, five measurement names and integer values 101–105 |

These checks validate encoding/decoding for the test messages. The synthetic UE and values are not radio measurements.

## Successful live subscription

After stopping DU, starting the new FlexRIC and restarting DU, E2 Setup again completed and RIC accepted KPM function ID 2. The new C xApp output included:

```text
[xApp]: Registered E2 Nodes = 1
Connected E2 nodes = 1
xApp subscribes RAN Func ID 2 in E2 node idx 0, nb_id 411
[xApp]: SUBSCRIPTION RESPONSE rx
[xApp]: Successfully subscribed to RAN_FUNC_ID 2
[xApp]: E42 SUBSCRIPTION DELETE RESPONSE rx
Test xApp run SUCCESSFULLY
```

RIC remained running and handled subscription deletion. The monitor's approximately 10-second observation window and subsequent exit are expected for this revision.

## Next acceptance and diagnostics

Connect the RM500Q using the existing IPv4 procedure, generate private traffic, and run the Spark xApp during that traffic. Require actual gNB-DU UE IDs, metric values and the expected report cadence. The current successful run proves subscription lifecycle, not actual UE throughput telemetry.

Keep these runtime details in mind:

- CU and RIC must be available before DU. E2 Setup waits for F1 component data; restarting RIC requires restarting DU for a fresh E2 association.
- RLC metrics must be enabled for the UE reports used here. Without an eligible UE, subscription can succeed without periodic indications.
- `log.all_level` does not override the separate E2AP logger default. Set only `log.e2ap_level: debug` when more E2 evidence is needed, then restart DU after RIC.
- The example's printed KPM indication latency subtracts its collection timestamp as Unix microseconds, while this OCUDU uses an NTP fixed-point timestamp. That display requires conversion correction before being used as a latency result.
- The current provider accepts the example's S-NSSAI condition without enforcing a slice filter. Do not treat its output as validated per-slice isolation.

Use narrow E2/KPM excerpts when diagnosing failures; keep authentication material and complete RAN INFO logs outside Git. Stop traffic/xApp and the laptop PDU first, then DU, RIC, CU and core; preserve MongoDB data.
