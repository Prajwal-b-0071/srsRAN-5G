# srsRAN gNB with a LimeSDR Mini V2 (LimeSuiteNG): COTS UE bring-up

A COTS UE registers with the core through the srsRAN gNB using a LimeSDR Mini V2 as the radio. This document explains
why it initially failed, what was changed in srsRAN to fix it, and how to run and check it.

## Result

With the changes below, a COTS UE (IMSI `001010000000003`) completes the full attach through the LimeSDR:

PRACH → RAR → Msg3 → RRC Setup → RRC Setup Complete → NAS registration → security → `InitialContextSetupResponse`

The AMF reports the UE as `5GMM-REGISTERED`, and the UE stayed connected for the whole 100 s test with no radio link
failure. PDU session setup still fails, for a core network reason (see [Open issues](#open-issues)).

## Setup

| Item | Value |
|---|---|
| srsRAN | This repository: the srsRAN Project from the `limesuiteng` branch of `github.com/myriadrf/srsRAN_Project` (commit `b9c8feb11a`), plus the changes below |
| Radio | LimeSDR Mini V2 over USB 3.0 (enumerates as `dspR_test`, gateware target board `LimeSDR-Mini`) |
| Radio library | LimeSuiteNG 0.3.0 (`/usr/local`) |
| Cell | Band n78, TDD, 20 MHz, 30 kHz SCS, `dl_arfcn` 632628 (3489.42 MHz), SSB at 3483.84 MHz, PCI 1, 1T1R |
| Core | OAI 5G core (`oai-cn-fed`, `docker-compose-basic-nrf-ebpf.yaml`): AMF 192.168.70.132, PLMN 00101, TAC 1, SST 1 |
| Reference | The same gNB with a USRP B210 and the stock `configs/gnb_rf_b200_tdd_n78_20mhz.yml` (core settings changed) attaches the same UE |

## Why the LimeSDR did not connect the UE

Because the USRP works with the same gNB and cell configuration, the problem had to be LimeSDR specific: the srsRAN
LimeSuiteNG driver, srsRAN timing assumptions, or the LimeSDR hardware. There were several problems, stacked in the
order a UE attaches; each fix exposed the next one.

### 1. The gNB stopped detecting PRACH after about 20 s

**Symptom.** After roughly 20 s the log fills with `PRACH buffer pool depleted. Ignoring PRACH request`, 100 times per
second, and no UE is ever detected again.

**Cause.** A bug in srsRAN. When a PRACH capture request reaches the lower PHY late, the worker reports it and goes
idle, but keeps its PRACH buffer (`buffer.reset()` only runs in `stop()`). The pool has only 2 buffers, so two late
requests on spare workers block PRACH permanently. LimeSDR timing jitter makes late requests common; with a USRP they
are rare.

**Fix.** Release the buffer on the late path, in `lib/phy/lower/processors/uplink/prach/prach_processor_worker.cpp`.

### 2. The downlink was damaged on its way to the LimeSDR

**Symptom.** In 2-minute runs with the unmodified branch, no PRACH was ever detected: the UE could not use the cell.

**Cause A: the Lime driver skipped empty slots.** `radio_limesuiteng_tx_stream.cpp` returned early for empty buffers
(uplink slots and slots with nothing to send). LimeSuiteNG packs consecutive writes into 1360-sample USB packets and
stamps each packet with the timestamp of its first sample (`TRXLooper::TransmitPacketsLoop`). The tail of one burst was
therefore merged into the first packet of the next burst under an old timestamp. The FPGA drops that packet as late, so
every downlink burst lost its beginning. The same function also read `tx_start` where it meant `tx_end`.

**Fix A.** Write every buffer, including zero-filled ones, as srsRAN's UHD driver does in continuous mode, and use
`tx_end` correctly.

**Cause B: the transmit lead was too short.** The lower PHY processes each downlink slot at most 1 ms ahead of the last
received sample (hard-coded in `lower_phy_factory.cpp`). The LimeSDR USB round trip takes about 3–4 ms, so part of the
samples reached the device after their transmission time. OAI works with the same LimeSDR because it runs 6 slots (3 ms)
ahead (`sl_ahead = 6` in `nr-softmodem.c`).

**Fix B.** A new option, `ru_sdr: expert_cfg: rx_to_tx_max_delay_us` (default 1000, the old behaviour). It is set to
3000 for the LimeSDR.

**Result.** The UE detects the cell and starts attaching: PRACH detected, RAR sent, Msg3 decoded, RRC Setup sent. The UE
acknowledges the RRC Setup on PUCCH (`ack=1`, SINR 11 dB), which confirms the downlink now works.

### 3. The UE overloaded the LimeSDR receiver

**Symptom.** After the RRC Setup, the UE's `rrcSetupComplete` on PUSCH was never decoded (dozens of retransmissions,
SINR -40 dB), so every attempt timed out. Small, robust signals were fine: Msg3 on 3 PRBs and PUCCH ACK/SR/CSI.

**Evidence** (`--phy_level debug`):

| | Msg3 (decoded) | Failing PUSCH |
|---|---|---|
| `epre` (received energy per RE) | -9.3 dB | **-0.3 dB**, near full scale |
| `sinr_ch_est` | +5.9 dB | -14.3 dB |
| EVM per symbol | about 0.6 | **15 to 36** |

The UE reports a negative power headroom at its maximum power (`SE_PHR: ph=[-8..-2)dB p_cmax=20 dBm`), so it transmits
at full power into a receiver at maximum gain a short distance away. Closed-loop PUSCH power control (copied from the
OAI configuration) made it worse, because it kept asking the UE for more power when the SINR was low.

**Fix.** `rx_gain: 30` instead of 50 (the LNA stays near maximum, the PGA is 19 dB lower); 20 made the uplink too weak.
Closed-loop PUSCH power control is left off, which is the srsRAN default.

**Result.** The first successful registration, but the link was fragile: one attempt failed after security setup with
`RLC max ReTxs reached` and `100 consecutive HARQ-ACK KOs`.

### 4. A spur on the LimeSDR receive DC subcarrier

**Symptom.** PUSCH allocations that span the carrier centre almost always failed; allocations away from it usually
decoded (run with `rx_gain 30`):

| PUSCH allocations | Decoded | Typical SINR |
|---|---|---|
| Not spanning the centre, e.g. `[0,7)`, `[8,11)`, `[8,21)`, `[28,43)` | 25 of 39 (64%) | -3 to +6 dB |
| Spanning the centre, e.g. `[8,32)`, `[8,43)`, `[0,51)` | 33 of 605 (5%) | -15 to -27 dB |

**Evidence.** The received uplink resource grid was recorded with `--phy_rx_symbols_filename` and the power of each of
the 612 subcarriers was measured (method under [Diagnostics](#diagnostics)):

| | Power |
|---|---|
| Median subcarrier, uplink slots without PUSCH | -25 dB |
| **Subcarrier 306 (carrier centre, DC)**, same slots | **+15 dB**, 40 dB above the noise floor |
| UE PUSCH, per PRB | 0 to +6 dB |
| Subcarrier 306 while the UE transmits | **+28 dB** |

The spur is there even though the LMS7002M automatic RX DC corrector is enabled (`DC_BYP_RXTSP = 0` in the Mini
defaults). The likely source is local oscillator leakage from the LimeSDR's own transmitter, which uses the same
frequency in TDD, plus the UE's own DC.

**Cause.** srsRAN zeroes the DC subcarrier for PUSCH data (`pusch_processor_impl.cpp`), but only after channel
estimation. The DC subcarrier carries a DM-RS pilot. `port_channel_estimator_average_impl.cpp` sums the pilot
residuals of the whole allocation into one noise estimate, so a pilot 20 to 28 dB above the UE signal inflates the noise
estimate of the entire PUSCH. The equalizer then scales all soft bits down and decoding fails.

**Fix.** A new option, `ru_sdr: expert_cfg: null_ul_dc_subcarrier` (default off). When enabled, the uplink OFDM
demodulator zeroes the DC subcarrier (DFT bin 0) as each symbol is received, before any PUSCH, PUCCH or SRS processing.

**Result.** PUSCH spanning the centre went from 5% to about 55% decoded (SINR -25 dB to about -8 dB); HARQ
retransmissions recover the rest. The UE registers on the first attempt and stays connected (0 radio link failures in
100 s).

### Progress across the fixes

| Configuration | PRACH detected | RRC Setup Complete | Registered | Notes |
|---|---|---|---|---|
| Unmodified branch (base config, and with `cell_cfg_max_32_ues.yml`) | 0 | 0 | 0 | 2-minute runs each |
| Fixes 1–2 and OAI-Lime SIB1 values, `rx_gain 50`, closed loop on | 3 | 0 | 0 | Msg3 decoded, RRC Setup acknowledged, PUSCH SINR -40 dB |
| Plus `rx_gain 30`, closed loop off | 16 | 2 | 1 | One attempt dropped by radio link failure |
| `rx_gain 20` instead | 8 | 3 | 0 | PUSCH 15 decoded / 344 failed |
| **Plus `null_ul_dc_subcarrier` (final configuration)** | 1 | 1 | **1** | 0 radio link failures, PUSCH 53 decoded / 46 failed |

### Investigated and ruled out

- **The FPGA "TX packet dropped" flag as a drop counter.** The gateware sets a sticky flag in every received packet
  header; it is cleared by a one-cycle pulse that crosses from the RX to the TX clock domain
  (`MultiReg(pct_loss_flg_clr, ...)` in `tx_path_top.py`). Its rate is not a reliable drop count.
- **LimeSuiteNG `tsAdvance` statistics as a lateness measure.** They are measured against the last RX timestamp the host
  has seen, not the FPGA clock.
- **Padding of partial USB packets breaking timestamps.** LimeSuiteNG merges consecutive writes into full packets, so
  continuous transmission does not create padded partial packets.
- **The LMS7002M RX DC corrector being disabled.** It is enabled in the Mini defaults; the spur remains regardless.
- **The `cell_cfg_max_*_ues.yml` files.** They only size PUCCH/CSI resources for many UEs and change the TDD pattern;
  they do not affect initial access. At the Mini's bandwidths, only `cell_cfg_max_32_ues.yml` (20 or 30 MHz) and
  `cell_cfg_max_64_ues.yml` (30 MHz) start. `cell_cfg_pucch_narrow_bw.yml` works at 10, 20 and 30 MHz.
- **Power-headroom bandwidth adaptation (`enable_phr_bw_adaptation`) and removing `p_max`.** No clear improvement: the
  UE still reported `p_cmax` 20 dBm, and the first grants stayed 35 PRBs wide.

## Changes

All changes are in the latest commit of this repository, on top of the unmodified srsRAN import: 14 source files
(+64/-13 lines), one new configuration file, and these documents (`git show --stat HEAD`). Both new options default to
the previous behaviour, so the USRP configurations are unaffected.

### Code

| Change | Files |
|---|---|
| PRACH late-request buffer leak | `lib/phy/lower/processors/uplink/prach/prach_processor_worker.cpp` |
| Lime driver writes every buffer; `tx_end` fixed | `lib/radio/limesuiteng/radio_limesuiteng_tx_stream.cpp` |
| Option `rx_to_tx_max_delay_us` | `include/srsran/phy/lower/lower_phy_configuration.h`, `lib/phy/lower/lower_phy_factory.cpp`, `apps/units/flexible_o_du/split_8/helpers/ru_sdr_config.h`, `ru_sdr_config_cli11_schema.cpp`, `ru_sdr_config_translator.cpp` |
| Option `null_ul_dc_subcarrier` | `include/srsran/phy/lower/modulation/ofdm_demodulator.h`, `lib/phy/lower/modulation/ofdm_demodulator_impl.h`, `ofdm_demodulator_impl.cpp`, `include/srsran/phy/lower/processors/uplink/puxch/puxch_processor_factories.h`, `lib/phy/lower/processors/uplink/puxch/puxch_processor_factories.cpp`, `include/srsran/phy/lower/processors/uplink/uplink_processor_factories.h`, `lib/phy/lower/processors/uplink/uplink_processor_factories.cpp`, `lower_phy_configuration.h`, `lower_phy_factory.cpp`, and the three `ru_sdr_config*` files above |

The key parts:

```cpp
// prach_processor_worker.cpp, late PRACH request
notifier->on_prach_request_late(prach_context);
// Return the PRACH buffer to the pool, otherwise it stays held by this idle worker.
buffer.reset();
```

```cpp
// radio_limesuiteng_tx_stream.cpp, transmit(): no early return for empty buffers any more
unsigned start_padding = inmeta.tx_start.value_or(0);
// tx_end is the sample index where the signal ends, not a padding length.
unsigned end_index = inmeta.tx_end.value_or(data.get_nof_samples());
unsigned nsamples  = end_index - start_padding;
...
meta.flags = inmeta.tx_end.has_value() ? lime::StreamTxMeta::EndOfBurst : 0;
```

```cpp
// lower_phy_factory.cpp
unsigned rx_to_tx_max_delay = config.srate.to_kHz() * config.rx_to_tx_max_delay_us / 1000U + tx_time_offset;
```

```cpp
// ofdm_demodulator_impl.cpp, after the DFT and phase compensation
if (null_dc) {
  compensated_output[0] = 0;   // DFT bin 0 is the DC subcarrier (grid subcarrier bw_rb * 12 / 2)
}
```

### New configuration options

| Option (`ru_sdr: expert_cfg:`) | Default | LimeSDR value | Meaning |
|---|---|---|---|
| `rx_to_tx_max_delay_us` | 1000 | 3000 | How far, in µs, downlink processing may run ahead of the last received sample. Range 1000–10000. |
| `null_ul_dc_subcarrier` | false | true | Zero the DC subcarrier of the received PUSCH/PUCCH/SRS symbols. |

### Configuration file

`configs/gnb_rf_limesdr_mini_v2_tdd_n78_20mhz.yml` is the stock `gnb_rf_b200_tdd_n78_20mhz.yml` with:

- **Radio:** `device_driver: limesuiteng`, LimeSuiteNG `device_args` (RX path `LNAH`, TX path `BAND1`, I12 link, DC/IQ
  calibration), `tx_gain: 50`, `rx_gain: 30`. LimeSuiteNG gains are gain-table rows 0–50, not dB. The RX and TX paths
  must be set, because LimeSuiteNG leaves both RF switches open by default.
- **Expert options:** `rx_to_tx_max_delay_us: 3000`, `null_ul_dc_subcarrier: true`.
- **SIB1 values of the working OAI LimeSDR configuration:** SSB block power -5 dBm, PRACH target -96 dBm, power ramping
  2 dB with up to 10 attempts, msg3 delta 1, PUSCH p0 -86 dBm, PUCCH p0 -94 dBm, `p_max` 20 dBm. The RA response window
  is 20 slots, because srsRAN rejects windows above 10 ms (OAI used 40 ms).
- **Core network:** AMF 192.168.70.132, bind address 192.168.70.129, TAC 1.

```yaml
ru_sdr:
  device_driver: limesuiteng
  device_args: 'port0:"dev0",port0_max_channels_to_use:1,port0_rx_path:"LNAH",port0_tx_path:"BAND1",port0_linkFormat:"I12",port0_rx_calibration:"dciq",port0_tx_calibration:"dciq",logLevel:2'
  srate: 23.04
  tx_gain: 50
  rx_gain: 30
  expert_cfg:
    rx_to_tx_max_delay_us: 3000
    null_ul_dc_subcarrier: true

cell_cfg:
  dl_arfcn: 632628
  band: 78
  channel_bandwidth_MHz: 20
  common_scs: 30
  plmn: "00101"
  tac: 1
  pci: 1
  ssb:
    ssb_block_power_dbm: -5
  ul_common:
    p_max: 20
  prach:
    preamble_rx_target_pw: -96
    power_ramping_step_db: 2
    preamble_trans_max: 10
    ra_resp_window: 20
  pusch:
    msg3_delta_preamble: 1
    p0_nominal_with_grant: -86
  pucch:
    p0_nominal: -94
```

## Running

Build (only needed after code changes):

```bash
cd ~/srsRAN_Project/build
make -j4 gnb
```

Run with the LimeSDR attached and the core running:

```bash
cd ~/srsRAN_Project/build/apps/gnb
sudo ./gnb -c ~/srsRAN_Project/configs/gnb_rf_limesdr_mini_v2_tdd_n78_20mhz.yml log --mac_level info --rrc_level info --ngap_level info
```

Check the attach in a second terminal:

```bash
tail -f /tmp/gnb.log | grep -E "prach\(ra-rnti|rrcSetupComplete|InitialContextSetupResponse|RLF"
docker logs oai-amf 2>&1 | grep "5GMM-" | tail -2
```

A successful attach shows, in order: `prach(ra-rnti=...)`, `rrcSetupRequest`, `rrcSetupComplete`,
`InitialUEMessage`, `securityModeComplete`, `InitialContextSetupResponse`, and `5GMM-REGISTERED` in the AMF.

## Diagnostics

**PHY decode results.** Add `--phy_level info` to the `log` options to get one line per PRACH, PUSCH and PUCCH with
`crc`, `sinr` and ACK/SR results. `--phy_level debug` adds `epre`, `rsrp`, `sinr_ch_est` and per-symbol EVM for PUSCH.

**UE power headroom.** With `--mac_level info`, `SE_PHR: ph=[..)dB p_cmax=[..)dBm` in the uplink MAC lines shows
whether the UE is power-limited (negative `ph`).

**Received resource grid.** Add `--phy_rx_symbols_filename <file>` to the `log` options. Each uplink slot is written as
14 symbols × 612 subcarriers of complex float32 (at 20 MHz), and its file offset is logged as
`RX_SYMBOL: sector=0 offset=<o> size=<n>` (needs `--phy_level info`). This is about 40 MB/s, so record only a few
seconds. Per-subcarrier power, which is how the DC spur was found:

```python
import re, numpy as np
NSC, NSYM = 612, 14
grid = np.fromfile("rxgrid.bin", dtype=np.complex64)
offsets = [int(m[1]) for m in re.finditer(r"RX_SYMBOL: sector=0 offset=(\d+)", open("gnb.log").read())]
power = np.mean([np.abs(grid[o:o + NSC * NSYM].reshape(NSYM, NSC)) ** 2 for o in offsets], axis=(0, 1))
db = 10 * np.log10(power + 1e-20)
print("median %.1f dB, subcarrier 306 (DC) %.1f dB" % (np.median(db), db[306]))
```

## Open issues

- **Uplink margin is small.** The UE reports 6 to 14 dB less power headroom than it needs (`p_cmax` 20 dBm) for the wide
  early grants (35 PRBs), so about half of those PUSCH need HARQ retransmissions. Narrower early grants or more
  downlink/uplink link budget would help.
- **PDU session setup fails in the core, not the RAN.** The UE requests DNN `ims` (IPv4v6), which srsRAN rejects with
  `Unsupported PDU Session Type: ipv4v6`, and DNN `internet`, which the core does not know. Registration is unaffected.
- **No LimeSDR found means an assert, not an error message.** If LimeSuiteNG reports `No connected devices discovered`,
  the driver continues without a device and aborts on `Stream identifier ... exceeds the number of baseband gateways`.
  Check with `limeDevice`, and unplug and replug the LimeSDR if it is not listed.
- **The tests above ran without real-time priority,** because they were run without `sudo`. Run with `sudo` for normal
  operation.
