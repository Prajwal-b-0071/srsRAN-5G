# srsRAN gNB with a LimeSDR Mini V2: build and run guide

Step-by-step procedure to build the srsRAN gNB with LimeSuiteNG support and the LimeSDR fixes of this repository, and
to run a 5G cell that a COTS UE can attach to. Why each fix is needed is explained in
[LIMESDR_MINI_V2_COTS_BRINGUP.md](LIMESDR_MINI_V2_COTS_BRINGUP.md).

Tested on Ubuntu 22.04.5 LTS (8 CPU cores, 15 GB RAM) with a LimeSDR Mini V2 on USB 3.0 and the OAI 5G core
(`oai-cn-fed`) running on the same machine.

## Contents

1. [What you need](#1-what-you-need)
2. [Install the build dependencies](#2-install-the-build-dependencies)
3. [Build and install LimeSuiteNG](#3-build-and-install-limesuiteng)
4. [Check the LimeSDR](#4-check-the-limesdr)
5. [Get the source](#5-get-the-source)
6. [Configure and build srsRAN](#6-configure-and-build-srsran)
7. [Start the 5G core](#7-start-the-5g-core)
8. [Run the gNB with the LimeSDR](#8-run-the-gnb-with-the-limesdr)
9. [Attach the COTS UE](#9-attach-the-cots-ue)
10. [Check the attach](#10-check-the-attach)
11. [Stop](#11-stop)
12. [Troubleshooting](#12-troubleshooting)

## 1. What you need

| Item | Details |
|---|---|
| PC | Ubuntu 22.04, a USB 3.0 port, `sudo` rights. Compiling srsRAN needs a lot of memory; on 15 GB of RAM use 4 parallel jobs. |
| Radio | LimeSDR Mini V2 with antennas on the TX and RX ports used below (`BAND1` TX, `LNAH` RX). |
| 5G core | OAI 5G core in `~/oai-cn-fed` (Docker), with the UE's SIM subscribed (PLMN 00101). |
| UE | A 5G SA phone that supports band n78, with a test SIM for PLMN 00101. |

## 2. Install the build dependencies

```bash
sudo apt-get update
sudo apt-get install -y git cmake make gcc g++ pkg-config \
  libfftw3-dev libmbedtls-dev libsctp-dev libyaml-cpp-dev
```

Optional: `libzmq3-dev` adds the ZMQ test radio, and UHD adds USRP support. srsRAN builds without them.

## 3. Build and install LimeSuiteNG

Skip this step if LimeSuiteNG is already installed: check that
`/usr/local/lib/cmake/limesuiteng/limesuitengConfig.cmake` exists and that `limeDevice` runs.

```bash
cd ~
git clone https://github.com/myriadrf/LimeSuiteNG.git
cd LimeSuiteNG
sudo ./install_dependencies.sh
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j4
sudo cmake --install build
sudo ldconfig
```

The install also adds the USB udev rules (`/etc/udev/rules.d/65-limesuiteng-usb.rules`). Reload them so the LimeSDR can
be used without unplugging it:

```bash
sudo udevadm control --reload-rules
sudo udevadm trigger
```

Check the install:

```bash
ls /usr/local/lib/cmake/limesuiteng/limesuitengConfig.cmake
ldconfig -p | grep liblimesuiteng
```

> The tests in the bring-up document used the LimeSuiteNG already installed on the test PC, built from `~/LimeSuiteNG`
> (branch `develop`, commit `0555d373` of the author's LimeSuiteNG fork, with local changes). A build of upstream `myriadrf/develop`
> is expected to work but was not tested.

## 4. Check the LimeSDR

Plug the LimeSDR Mini V2 into a **USB 3.0** port, then:

```bash
limeDevice
limeDevice --full
```

Expected: one device, with `media=USB3.0`, and `Gateware target board : LimeSDR-Mini` in the `--full` output. On the
test PC it is listed as:

```text
0: dspR_test, media=USB3.0, addr=0403:601f, serial=00000000000013
```

If it is missing or shows USB 2.0, use another port or cable and check again.

## 5. Get the source

This repository already contains the srsRAN Project with the LimeSuiteNG driver and all LimeSDR fixes. If an old
`~/srsRAN_Project` exists, move it away first:

```bash
mv ~/srsRAN_Project ~/srsRAN_Project.old
```

Clone:

```bash
cd ~
git clone https://github.com/Prajwal-b-0071/srsRAN-5G.git srsRAN_Project
cd ~/srsRAN_Project
git log --oneline
```

The history has three commits: the repository creation, the import of the unmodified srsRAN Project (from the
`limesuiteng` branch of `github.com/myriadrf/srsRAN_Project`, commit `b9c8feb11a`), and the LimeSDR changes. To see
exactly what the LimeSDR changes are:

```bash
git show --stat HEAD
```

What the LimeSDR commit adds to the imported srsRAN Project:

| Fix | Files |
|---|---|
| PRACH buffers released for late PRACH requests | `lib/phy/lower/processors/uplink/prach/prach_processor_worker.cpp` |
| Lime driver sends every slot, `tx_end` bug fixed | `lib/radio/limesuiteng/radio_limesuiteng_tx_stream.cpp` |
| New option `ru_sdr: expert_cfg: rx_to_tx_max_delay_us` (transmit lead) | lower PHY configuration/factory and `ru_sdr_config*` files |
| New option `ru_sdr: expert_cfg: null_ul_dc_subcarrier` (zero the uplink DC subcarrier) | uplink OFDM demodulator, PUxCH and uplink processor factories, lower PHY configuration/factory, `ru_sdr_config*` files |
| LimeSDR gNB configuration | `configs/gnb_rf_limesdr_mini_v2_tdd_n78_20mhz.yml` |

The new options default to the original behaviour, so the USRP configurations work exactly as before.

## 6. Configure and build srsRAN

```bash
mkdir -p ~/srsRAN_Project/build
cd ~/srsRAN_Project/build
cmake .. -DCMAKE_BUILD_TYPE=Release -DENABLE_LIMESUITENG=ON
```

Check that CMake found LimeSuiteNG:

```bash
grep -E "ENABLE_LIMESUITENG:|limesuiteng_DIR" CMakeCache.txt
```

Expected:

```text
ENABLE_LIMESUITENG:BOOL=ON
limesuiteng_DIR:PATH=/usr/local/lib/cmake/limesuiteng
```

Build the gNB:

```bash
make -j4 gnb
```

The build takes a while. `-j4` keeps memory use safe on a 15 GB machine; with more RAM you can use `-j$(nproc)`. It ends
with:

```text
[100%] Linking CXX executable gnb
[100%] Built target gnb
```

Check the result:

```bash
ldd ~/srsRAN_Project/build/apps/gnb/gnb | grep limesuiteng
~/srsRAN_Project/build/apps/gnb/gnb ru_sdr expert_cfg --help | grep -E "rx_to_tx_max_delay_us|null_ul_dc_subcarrier"
```

Expected: `liblimesuiteng.so... => /usr/local/lib/liblimesuiteng.so...`, and both new options listed:

```text
  --rx_to_tx_max_delay_us UINT:UINT in [1000 - 10000] [1000]
  --null_ul_dc_subcarrier BOOLEAN [false]
```

## 7. Start the 5G core

The gNB configuration expects the OAI core on this PC: AMF `192.168.70.132`, with the gNB on the `demo-oai` bridge
(`192.168.70.129`), PLMN 00101, TAC 1, SST 1.

The committed eBPF compose file points the SMF at the UPF on `192.168.50.1` (a separate core PC). On a single PC the UPF
runs in host networking and is reachable at `192.168.70.129`, so create this override once:

```bash
cat > ~/oai-cn-fed/docker-compose/local-upf.override.yaml <<'EOF'
services:
    oai-smf:
        extra_hosts: !override
            - "oai-upf:192.168.70.129"
EOF
```

Start the core and wait until every container is healthy:

```bash
cd ~/oai-cn-fed/docker-compose
docker compose -f docker-compose-basic-nrf-ebpf.yaml -f local-upf.override.yaml up -d --wait
docker ps --format '{{.Names}}  {{.Status}}'
ip -br addr show demo-oai
```

Expected: 9 containers `(healthy)`, and `demo-oai` with address `192.168.70.129/26`.

## 8. Run the gNB with the LimeSDR

Before starting, check that no other gNB is running and that the LimeSDR is visible:

```bash
pgrep -a gnb || echo "no gnb running"
limeDevice
```

Start the gNB. Open a **new** terminal, or `cd` again: a terminal that was inside a deleted `~/srsRAN_Project` cannot
find `./gnb`.

```bash
cd ~/srsRAN_Project/build/apps/gnb
sudo ./gnb -c ~/srsRAN_Project/configs/gnb_rf_limesdr_mini_v2_tdd_n78_20mhz.yml \
  log --mac_level info --rrc_level info --ngap_level info
```

`sudo` gives the gNB real-time priority, which it needs. Expected output:

```text
--== srsRAN gNB (commit b9c8feb11a) ==--
Lower PHY in triple executor mode.
Available radio types: uhd, zmq and limesuiteng.
...
Connected: dspR_test, media=USB3.0, addr=0403:601f, serial=00000000000013
Rx calibration finished
Cell pci=1, bw=20 MHz, 1T1R, dl_arfcn=632628 (n78), dl_freq=3489.42 MHz, dl_ssb_arfcn=632256, ul_freq=3489.42 MHz

N2: Connection to AMF on 192.168.70.132:38412 completed
==== gNB started ===
```

LimeSuiteNG also prints `Key:Value{...}` lines for the device settings, and periodic `Rx| Loss / Tx| Late` and
`USB ep:...` statistics lines. These are normal.

What the configuration does:

| Setting | Value | Why |
|---|---|---|
| `device_driver` | `limesuiteng` | LimeSDR through LimeSuiteNG |
| `device_args` | RX `LNAH`, TX `BAND1`, link `I12`, DC/IQ calibration | Paths for 3.5 GHz; LimeSuiteNG leaves the RF switches open unless they are set |
| `tx_gain` / `rx_gain` | 50 / 30 | LimeSuiteNG gain-table rows 0–50, not dB. RX at 50 is overloaded by a nearby UE, 20 is too weak. |
| `rx_to_tx_max_delay_us` | 3000 | Transmit 3 ms ahead of reception, to cover the LimeSDR USB latency |
| `null_ul_dc_subcarrier` | true | Remove the LimeSDR receive DC spur that corrupts PUSCH decoding |
| Cell | n78, 20 MHz, 3489.42 MHz | Same cell as the stock USRP configuration |
| SIB1 power and RACH values | SSB -5 dBm, PRACH target -96 dBm, ramping 2 dB × 10, msg3 delta 1, p0 -86/-94 dBm, `p_max` 20 | Values of the working OAI LimeSDR configuration |

To see the full logs in the terminal instead of `/tmp/gnb.log`, add these to the `log` options:
`--filename stdout --all_level info --ngap_level debug --rrc_level debug --phy_level warning --rlc_level warning --pdcp_level warning`.

## 9. Attach the COTS UE

1. Put the SIM of a subscriber provisioned in the core (for example IMSI `001010000000003`) in the phone.
2. Enable 5G SA and make sure band n78 is allowed.
3. Keep the phone about 1–2 m from the LimeSDR antennas.
4. After `==== gNB started ===`, toggle airplane mode off and on so the phone searches again right away.
5. For data, the phone's APN must match a DNN configured in the core (see the note in step 10).

## 10. Check the attach

In a second terminal:

```bash
tail -f /tmp/gnb.log | grep -E "prach\(ra-rnti|rrcSetupRequest|rrcSetupComplete|InitialUEMessage|securityModeComplete|InitialContextSetupResponse|RLF"
```

A successful attach shows these lines, in this order:

| Log line | Meaning |
|---|---|
| `prach(ra-rnti=... tc-rnti=...)` | The gNB detected the UE's random access |
| `CCCH UL rrcSetupRequest` | Msg3 decoded |
| `DCCH UL rrcSetupComplete` | RRC connection set up |
| `Tx PDU ... InitialUEMessage` | Registration sent to the core |
| `DCCH UL securityModeComplete` | Security activated |
| `Tx PDU ... InitialContextSetupResponse` | **UE registered** |

Check the AMF:

```bash
docker logs oai-amf 2>&1 | grep "5GMM-" | tail -2
```

Expected: a row with the UE's IMSI and `5GMM-REGISTERED`.

In the gNB terminal, type `t` and press Enter to show live UE metrics (type `t` again to hide them).

> **Data sessions:** if the log shows `Unsupported PDU Session Type: ipv4v6` or the SMF rejects DNN `internet`, the UE
> is registered but the core's DNN configuration does not match the phone (the `ims` DNN is IPv4v6, and there is no
> `internet` DNN). This is a core network setting, not a LimeSDR problem.

## 11. Stop

Press `Ctrl+C` in the gNB terminal. `Stopping...` followed by `terminate called without an active exception` is a known
issue in the LimeSuiteNG driver's shutdown; the LimeSDR can be used again right away.

Stop the core when finished:

```bash
cd ~/oai-cn-fed/docker-compose
docker compose -f docker-compose-basic-nrf-ebpf.yaml -f local-upf.override.yaml down
```

## 12. Troubleshooting

| Message or symptom | Cause | What to do |
|---|---|---|
| `No connected devices discovered.` then `Assertion 'stream_id < bb_gateways.size()' failed` | LimeSuiteNG did not find the LimeSDR; the driver does not stop cleanly without a device | Run `limeDevice`. If the device is not listed, unplug and replug it (USB 3.0 port) and start again |
| `sudo: ./gnb: command not found` | The terminal is in a deleted or wrong folder | `cd ~/srsRAN_Project/build/apps/gnb` |
| `/home/.../lime_base.yml was not readable (missing?)` | A configuration file path is wrong | Check the path after `-c` |
| `Failed to bind UDP socket to 192.168.70.129:2152. Address already in use` | Another gNB is running | `pgrep -a gnb`, then stop it with `Ctrl+C` or `sudo pkill -INT -x gnb` |
| `Failed to bind UDP socket to 192.168.70.129:2152. Cannot assign requested address` | The core, and so the `demo-oai` bridge, is not running | Start the core (step 7) |
| No `N2: Connection to AMF ... completed` | The AMF is not reachable | `docker ps`; restart the core (step 7) |
| `the number of PRBs for PUCCH exceeds the 50% of the BWP PRBs` | An extra `cell_cfg_max_*_ues.yml` file does not fit the bandwidth | Use only the LimeSDR configuration, or an overlay that fits (see the bring-up document) |
| `RA Response Window (...) must be smaller than 10ms` | `ra_resp_window` above 20 slots at 30 kHz | Keep `ra_resp_window: 20` |
| `PRACH buffer pool depleted` repeating 100 times per second | The gNB was built from upstream srsRAN without these fixes (PRACH buffer leak) | Build from this repository (steps 5 and 6) |
| No `prach(ra-rnti=...)` lines at all | The UE is not attempting access | Toggle airplane mode; check SA mode, band n78, PLMN 00101 and the SIM subscription |
| Many `PUSCH ... crc=KO` with SINR around -25 dB | DC nulling is off | Check `null_ul_dc_subcarrier: true` in the configuration |
| PUSCH `crc=KO` with `epre` near 0 dB (`--phy_level debug`) | Receiver overloaded | Keep `rx_gain: 30`, or move the phone further away |
| `RLF detected` shortly after attach | Weak uplink | Keep the phone 1–2 m away with line of sight; closed-loop PUSCH power control must stay off |
