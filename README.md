srsRAN-5G: srsRAN Project gNB with LimeSDR Mini V2 support
==========================================================

This repository runs the srsRAN Project 5G gNB with a **LimeSDR Mini V2** radio (through LimeSuiteNG), with commercial
(COTS) 5G phones attaching to it. It adds the fixes and configuration needed for a COTS UE to complete registration
through the LimeSDR, which the unmodified software does not achieve.

What this repository adds:

- **LimeSuiteNG driver fix:** every slot is sent to the radio, so transmit bursts are no longer corrupted by stale
  timestamps.
- **Transmit lead option** `ru_sdr: expert_cfg: rx_to_tx_max_delay_us`, to cover the LimeSDR USB latency.
- **Uplink DC subcarrier nulling option** `ru_sdr: expert_cfg: null_ul_dc_subcarrier`, which removes the LimeSDR
  receive DC spur that otherwise breaks PUSCH decoding.
- **PRACH fix:** late PRACH requests no longer leak PRACH buffers.
- **Ready-to-use configuration:** [configs/gnb_rf_limesdr_mini_v2_tdd_n78_20mhz.yml](configs/gnb_rf_limesdr_mini_v2_tdd_n78_20mhz.yml).

Documentation:

- [LIMESDR_MINI_V2_SETUP_GUIDE.md](LIMESDR_MINI_V2_SETUP_GUIDE.md): step-by-step build and run procedure.
- [LIMESDR_MINI_V2_COTS_BRINGUP.md](LIMESDR_MINI_V2_COTS_BRINGUP.md): why the LimeSDR did not work, and what was
  changed.

Quick start (LimeSuiteNG installed, LimeSDR connected, 5G core running):

```bash
git clone https://github.com/Prajwal-b-0071/srsRAN-5G.git srsRAN_Project
cd srsRAN_Project && mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release -DENABLE_LIMESUITENG=ON
make -j4 gnb
cd apps/gnb
sudo ./gnb -c ../../../configs/gnb_rf_limesdr_mini_v2_tdd_n78_20mhz.yml
```

Origin and license: this is a modified version of the [srsRAN Project](https://github.com/srsran/srsRAN_Project) by
Software Radio Systems, based on the LimeSuiteNG integration on the `limesuiteng` branch of
[myriadrf/srsRAN_Project](https://github.com/myriadrf/srsRAN_Project). It is distributed under the same GNU Affero General
Public License v3 (see [LICENSE](LICENSE) and [COPYRIGHT](COPYRIGHT)). LimeSDR Mini V2 support modified by Prajwal B,
September 2026.

The original srsRAN Project README follows.

---

srsRAN Project
==============

[![Build Status](https://github.com/srsran/srsRAN_Project/actions/workflows/ccpp.yml/badge.svg?branch=main)](https://github.com/srsran/srsRAN_Project/actions/workflows/ccpp.yml)
[![OpenSSF Best Practices](https://www.bestpractices.dev/projects/7868/badge)](https://www.bestpractices.dev/projects/7868)

The srsRAN Project is a complete 5G RAN solution, featuring an ORAN-native CU/DU developed by [SRS](http://www.srs.io).

The solution includes a complete L1/2/3 implementation with minimal external dependencies. Portable across processor architectures, the software has been optimized for x86 and ARM. srsRAN follows the 3GPP 5G system architecture implementing the functional splits between distributed unit (DU) and centralized unit (CU). The CU is further disaggregated into control plane (CU-CP) and user-plane (CU-UP).

See the [srsRAN Project](https://www.srsran.com/) for information, guides and project news.

Build instructions and user guides - [srsRAN Project documentation](https://docs.srsran.com/projects/project).

Community announcements and support - [Discussion board](https://www.github.com/srsran/srsran_project/discussions).

Features and roadmap - [Features](https://docs.srsran.com/projects/project/en/latest/general/source/2_features_and_roadmap.html).

Build Preparation
-----------------

### Dependencies

* Build tools:
  * cmake:               <https://cmake.org/>
* Mandatory requirements:
  * libfftw:             <https://www.fftw.org/>
  * libsctp:             <https://github.com/sctp/lksctp-tools>
  * yaml-cpp:            <https://github.com/jbeder/yaml-cpp>
  * mbedTLS:             <https://www.trustedfirmware.org/projects/mbed-tls/>
* Optional requirements:
  * googletest:          <https://github.com/google/googletest/>
    * You can enable test building by using the cmake option `-DBUILD_TESTING=On`. GoogleTest is only mandatory when building with tests.

You can install the build tools and mandatory requirements for some example distributions with the commands below:

<details open>
<summary><strong>Ubuntu 22.04</strong></summary>

```bash
sudo apt-get install cmake make gcc g++ pkg-config libfftw3-dev libmbedtls-dev libsctp-dev libyaml-cpp-dev
```

</details>
<details>
<summary><strong>Fedora</strong></summary>

```bash
sudo yum install cmake make gcc gcc-c++ fftw-devel lksctp-tools-devel yaml-cpp-devel mbedtls-devel
```

</details>
<details>
<summary><strong>Arch Linux</strong></summary>

```bash
sudo pacman -S cmake make base-devel fftw mbedtls yaml-cpp lksctp-tools
```

</details>

#### Split-8

For Split-8 configurations, either UHD or ZMQ is required for the fronthaul interface. Both drivers are linked below, please see their respective documentation for installation instructions.

* UHD:                 <https://github.com/EttusResearch/uhd>
* ZMQ:                 <https://zeromq.org/>

#### Split-7.2

For Split-7.2 configurations no extra 3rd-party dependencies are required, only those listed above.

Optionally, DPDK can be installed for high-bandwidth low-latency scenarios. For more information on this, please see [this tutorial](https://docs.srsran.com/projects/project/en/latest/tutorials/source/dpdk/source/index.html#).

Build Instructions
------------------

Download and build srsRAN:

<details open>
<summary><strong>Vanilla Installation</strong></summary>

First, clone the srsRAN Project repository:

```bash
    git clone https://github.com/srsRAN/srsRAN_Project.git
```

Then build the code-base:

```bash
    cd srsRAN_Project
    mkdir build
    cd build
    cmake ../ 
    make -j $(nproc)
```

You can now run the gNB from ``srsRAN_Project/build/apps/gnb/``. If you wish to install the srsRAN Project gNB, you can use the following command:

```bash
    sudo make install
```

</details>

<details>
<summary><strong>ZMQ Enabled Installation</strong></summary>

Once ZMQ has been installed you will need build of srsRAN Project with the correct flags to enable the use of ZMQ.

The following commands can be used to clone and build srsRAN Project from source. The relevant flags are added to the ``cmake`` command to enable the use of ZMQ:

```bash
git clone https://github.com/srsran/srsRAN_Project.git
cd srsRAN_Project
mkdir build
cd build
cmake ../ -DENABLE_EXPORT=ON -DENABLE_ZEROMQ=ON
make -j $(nproc)
```

Pay extra attention to the cmake console output. Make sure you read the following line to ensure ZMQ has been correctly detected by srsRAN:

```bash
...
-- FINDING ZEROMQ.
-- Checking for module 'ZeroMQ'
--   No package 'ZeroMQ' found
-- Found libZEROMQ: /usr/local/include, /usr/local/lib/libzmq.so
...
```

</details>

<details>
<summary><strong>DPDK Enabled Installation</strong></summary>

Once DPDK has been installed and configured you will need to create a clean build of srsRAN Project to enable the use of DPDK.

If you have not done so already, download the code-base with the following command:

```bash
git clone https://github.com/srsRAN/srsRAN_Project.git
```

Then build the code-base, making sure to include the correct flags when running cmake:

```bash
cd srsRAN_Project
mkdir build
cd build
cmake ../ -DENABLE_DPDK=True -DASSERT_LEVEL=MINIMAL
make -j $(nproc)
```

</details>

### PHY Tests

PHY layer tests use binary test vectors and are not built by default. To enable, see the [docs](https://docs.srsran.com/projects/project/en/latest/user_manuals/source/installation.html).

Deploying srsRAN Project
------------------------

srsRAN Project can be run in two ways:

* As a monolithic gNB (combined CU & DU)
* With a split CU and DU

For exact details on running srsRAN Project in any configuration, see [the documentation](https://docs.srsran.com/projects/project/en/latest/user_manuals/source/running.html).

For information on configuring and running srsRAN for various different use cases,  check our [tutorials](https://docs.srsran.com/projects/project/en/latest/tutorials/source/index.html).
