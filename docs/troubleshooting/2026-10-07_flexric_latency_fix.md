# FlexRIC KPM v3 timestamp repair

Date: **2026-10-07**. Applies to FlexRIC commit `736508123fe4b5dc3db83fb5baf5f0a8e9b04fe8` and the current OCUDU `050a2bb` DU. See the [bring-up record](2026-10-07_flexric_bringup.md) for the earlier subscription codec repair and actual UE measurements.

The repair is applied to `/home/nyu/flexric`, and the affected `build-ocudu` targets compiled successfully. Offline regression checks and an isolated live subscription check passed. **The installed `/usr/local` KPM library and `xapp_oran_moni` now match the repaired build**, confirmed by read-only SHA-256 checks. Live UE report-age values remain pending.

## Cause and repair

The original monitor printed enormous negative `KPM-v3 ind_msg latency` values. OCUDU sends eight bytes containing a big-endian NTP 32.32 timestamp: seconds since 1900, followed by a fractional second. FlexRIC's old v3 decoder ordered the 32-bit halves incorrectly on this host, then the monitor treated the result as Unix microseconds. The five UE measurement values did not depend on this header timestamp conversion.

The reusable [patch](../../patches/flexric/73650812-kpm-timestamp.patch) changes the KPM v3 ASN boundary:

- The encoder converts internal Unix microseconds to NTP and writes eight explicit big-endian bytes.
- The decoder reads those bytes explicitly and converts NTP to Unix microseconds. It selects the NTP era nearest the receiving system clock, including the February 2036 rollover; the timestamp must be within approximately 68 years of that reference.
- A v3-local `static inline` helper preserves FlexRIC's native and emulator Unix-microsecond API. KPM v2 and global conversion helpers are unchanged.
- Both `xapp_oran_moni` and `xapp_all_moni` print a signed `report_age_us` value.

## Meaning of `report_age_us`

The display is the xApp callback's Unix time minus the decoded header time. It includes processing and queueing across DU, E2, RIC and E42, plus any difference between sender and receiver clocks. A negative value can still occur with clock offset or clock adjustments; it must remain signed.

In current OCUDU, `e2_indication_procedure.cpp` calls `get_indication_message()` before `get_indication_header()`. Packing the message clears measurements, and the Format 4 clear function updates `collet_start_time` before the header is packed. The transmitted timestamp therefore nearly marks report generation, despite the field's collection-start name. The difference is neither pure transport latency nor the full configured 1000 ms collection period.

OCUDU also computes the fraction using the truncated integer `2^32 / 1000000 = 4294`. Its timestamp can be up to approximately **226 microseconds early**. The display consequently does not establish microsecond measurement accuracy.

## Offline validation

The [regression test](../../tests/flexric_kpm_timestamp.c) passed these independent checks:

| Check | Result |
|---|---|
| OCUDU literal bytes `ee70c5803ffc4f60` | Unix `1791379200249943` microseconds |
| Callback time `1791379200251043` | Signed report age **1100 microseconds** |
| Values before, at and after NTP era rollover | Pass |
| All 1,000,000 fractional microseconds | Encoder/decoder round trips preserve the input |
| Actual ASN boundary | Decodes the independent OCUDU bytes and encodes canonical NTP bytes |
| Existing `test_enc_dec_kpm_sm` and `test_kpm_sm` | Pass |

The independent OCUDU vector was generated at Unix `1791379200.250000`; its decoded fraction is 57 microseconds early because of the source calculation above. These results are synthetic regression evidence, not a live radio-delay benchmark.

## Reproduction

Apply the patch only to an **unpatched checkout at the exact FlexRIC commit above**. The current Spark source already contains it. From the Falcon repository:

```bash
git -C /home/nyu/flexric rev-parse HEAD
git -C /home/nyu/flexric apply --check \
  /home/nyu/Desktop/Falcon_repo/Falcon/patches/flexric/73650812-kpm-timestamp.patch
git -C /home/nyu/flexric apply \
  /home/nyu/Desktop/Falcon_repo/Falcon/patches/flexric/73650812-kpm-timestamp.patch

cmake -S /home/nyu/flexric -B /home/nyu/flexric/build-ocudu \
  -DCMAKE_C_COMPILER=gcc -DCMAKE_BUILD_TYPE=Debug \
  -DE2AP_VERSION=E2AP_V3 -DKPM_VERSION=KPM_V3_00 \
  -DXAPP_DB=NONE_XAPP -DXAPP_MULTILANGUAGE=OFF
cmake --build /home/nyu/flexric/build-ocudu -j 8 \
  --target kpm_sm kpm_sm_static xapp_oran_moni xapp_all_moni \
  test_enc_dec_kpm_sm test_kpm_sm
```

Compile and run the standalone regression against the repaired helper:

```bash
gcc -std=c11 -O2 -Wall -Wextra -Werror \
  -I/home/nyu/flexric/src/sm/kpm_sm/kpm_sm_v03.00/ie/kpm_data_ie/kpm_ric_info \
  /home/nyu/Desktop/Falcon_repo/Falcon/tests/flexric_kpm_timestamp.c \
  -o /tmp/flexric_kpm_timestamp
/tmp/flexric_kpm_timestamp
```

For the actual ASN boundary variant, also define `FLEXRIC_ASN_BOUNDARY_TEST`, `ASN_DISABLE_OER_SUPPORT` and `ASN_DISABLE_JER_SUPPORT`, include the v3 `ie/asn`, `dec/dec_asn` and `enc/enc_asn` directories, and link `build-ocudu/src/sm/kpm_sm/kpm_sm_v03.00/libkpm_sm_static.a` followed by `-lm`. These ASN feature definitions match the library build. The existing checks are at:

```bash
/home/nyu/flexric/build-ocudu/test/encode_decode/sm/kpm/kpm_sm_v03.00/test_enc_dec_kpm_sm
/home/nyu/flexric/build-ocudu/test/sm/kpm_sm/kpm_sm_v03.00/test_kpm_sm
```

## Live subscription check and installation

Before installation, the new `xapp_oran_moni` ran against the existing RIC using a private service-model directory: the new KPM library plus the installed other plugins. It subscribed to function ID 2, received the deletion response and exited with `Test xApp run SUCCESSFULLY`. This approximately 10-second run contained no UE indications, so it did not verify an actual `report_age_us` value. CU, DU and RIC were not restarted during that preview, and the preview left installed files unchanged.

The initial agent installation attempt using `sudo -n` required a password and stopped before writing installed files. At that stage, the installed KPM library had SHA-256 `74f62b45b8ac37b770b20e53a39e0fa19eac5944ebb0b4a977e5a906b40f2711`.

Subsequent read-only verification confirms that both the built and installed KPM library now have SHA-256 `f7e62d2a135e587d67a39cddcc1270ec9ba0ec97df03a3f8dbf63124c46ed53d`. The built and installed `xapp_oran_moni` also match, with SHA-256 `95b2a3a6d5a2764efa8c3266d4a3b7fa75a35bb6700dd0cc43f9732a7591b7d3`. These checks establish deployment of the two artifacts needed for the next live UE check; the live `report_age_us` result is still unverified.

For reproduction on a host that still has the old installed artifacts, use the [installer](../../scripts/flexric/install_timestamp_fix.sh) in the operator's Spark terminal and enter the sudo password there:

```bash
sudo bash /home/nyu/Desktop/Falcon_repo/Falcon/scripts/flexric/install_timestamp_fix.sh
```

It backs up the existing KPM library and both C monitors under `/usr/local/share/flexric/backups/`, then installs new files through temporary files and renames. Running processes retain their existing mappings. RIC forwards the original KPM ASN bytes, so this current OCUDU setup can validate a newly launched repaired xApp without restarting CU/DU/RIC. Let any existing xApp finish before installation; a later RIC launch loads the new library. Do not use a full in-place `cmake --install` while the old shared library is mapped by a running process.

Verify the installed artifact matches the build:

```bash
cmp -s /home/nyu/flexric/build-ocudu/src/sm/kpm_sm/kpm_sm_v03.00/libkpm_sm.so \
  /usr/local/lib/flexric/libkpm_sm.so && printf 'KPM library: OK\n'
cmp -s /home/nyu/flexric/build-ocudu/examples/xApp/c/monitor/xapp_oran_moni \
  /usr/local/bin/flexric/xApp/c/xapp_oran_moni && printf 'KPM monitor: OK\n'
```

## Verification after installation

Keep the existing CU, DU and RIC running, and let any previous xApp finish. Reuse the Spark iperf server or start `iperf3 -s -B 10.45.0.1`. In the UE laptop's WWAN-configured Bash terminal, start the 30-second uplink; immediately run the installed Spark xApp with a fresh `KPM_LOG`. Its approximately 10-second collection window must overlap the traffic.

Follow the complete [Spark step 7a](../spark_quick_commands.md#7a-时间戳修复安装后复验) or [runbook stage 14a](../full_reproduction_runbook.md#14a-validate-ue-kpm-reports-during-traffic). The compact output must include actual UE IDs, `KPM-v3 report_age_us` values and `Test xApp run SUCCESSFULLY`. Successful exit alone means the subscription lifecycle passed; it does not verify UE report age. The command prints the new log path for reviewing the five measurement values or failures.

Installation is verified; live UE report-age values and a synchronized 30-second application benchmark remain pending. Keep the metric units and timestamp precision limits above when interpreting the eventual result.
