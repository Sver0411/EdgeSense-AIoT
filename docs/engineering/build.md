# Build — sensor node firmware

> **Status: `firmware-built`. Not flashed. Not run on a device.**
> A build proves the code compiles and links for the target. It proves nothing about
> behaviour on hardware.

---

## 1. Toolchain actually used

Recorded from the build that produced the artifacts below.

| Item | Value |
|---|---|
| ESP-IDF | **v5.4.4** (at `~/esp/esp-idf`) |
| Compiler | `xtensa-esp-elf-gcc (crosstool-NG esp-14.2.0_20260121) 14.2.0` |
| CMake | 3.30.2 (supplied by ESP-IDF tools) |
| Target | `esp32s3` |
| Host | macOS (darwin) |

Python version matters and is easy to get wrong: ESP-IDF's virtual environment is
`~/.espressif/python_env/idf5.4_py3.13_env`, so `python3` on `PATH` must be a 3.13
interpreter *before* `export.sh` runs. With the system 3.9 first, `export.sh` fails
looking for `idf5.4_py3.9_env` and `idf.py` never lands on `PATH`. The symptom is
"idf.py: command not found", which looks like a missing install and is not.

## 2. Reproducing it

```bash
env -i HOME="$HOME" \
  PATH="$HOME/.workbuddy/binaries/python/versions/3.13.12/bin:/usr/bin:/bin:/usr/sbin:/sbin" \
  TERM=xterm /bin/bash -c '
    export IDF_TOOLS_PATH="$HOME/.espressif"
    . "$HOME/esp/esp-idf/export.sh" >/dev/null
    cd firmware/sensor_node
    idf.py build
  '
```

`env -i` is not superstition: this machine injects a Python shim into interactive
shells that breaks several build tools. A clean environment removes that variable
from the equation entirely.

Only `idf.py build` is used. **`flash` is not run by any script in this repository**
and must not be, until the wiring has been verified on the board (see
docs/hardware/wiring.md).

## 3. Result

| Artifact | Size |
|---|---|
| `edgesense_sensor_node.bin` (app image) | **0x39B10 = 236,304 bytes** |
| App partition | 0x100000 (1 MiB), **77% free** |
| `bootloader.bin` | 0x51C0 = 20,928 bytes |
| ELF (with debug info) | 4,116,848 bytes |

Compile diagnostics: **one warning, none in EdgeSense's own code.**

```
firmware/common/vendor/adaptivesense/soil_moisture.c:35:20:
  warning: 'atten_name' defined but not used [-Wunused-function]
```

That file is vendored and never edited. The warning is why the host test script
compiles vendored sources without `-Werror` while compiling EdgeSense's own sources
with it: warnings in code we did not write are *reported*, not converted into our
build failure, and never silenced with `-Wno-*`.

## 4. What this build includes

| Unit | Origin | In this build |
|---|---|---|
| `edge_channel`, `edge_sensor_state`, `edge_sensor_map` | EdgeSense | yes |
| `edge_nodes`, `edge_config_generated.h` | EdgeSense (generated from YAML) | yes |
| `edge_protocol` | EdgeSense | yes |
| `edge_sensor_port` | EdgeSense | yes |
| `sensor.c`, `sensor_bus.c`, `sensor_sht30.c`, `sensor_bh1750.c`, `soil_moisture.c`, `sensor_supervisor.c`, `sht30_proto.c`, `bh1750_proto.c`, `soil_moisture_math.c` | vendored AdaptiveSense | yes |
| `protocol.c` (CRC-16) | vendored EventGuard-LoRa | yes |

**Not in this build**: LoRa transport (`e220.c`, `e220_stream_parser.c`) — the
radio path is a later integration; `faults.c` — the injection campaign is EXP-010;
SensorTrust, adaptive sampling, cross-channel fusion — later phases by design.

## 5. Build configuration decisions

| Setting | Value | Reason |
|---|---|---|
| Flash size | **16 MiB** | Board is ESP32-S3 N16R8. Documented from the hardware list; not read out from an EdgeSense board. |
| PSRAM | **off** | The working set is fixed-size frames of a few hundred bytes. A disabled PSRAM is one less thing that can differ between two nodes that must be identical. The earlier SensorTrust build made the same choice. |
| B/C difference | **none in code** | Nodes B and C build from this one project; identity comes from the generated config. |

## 6. What a build does not establish

- that the I2C bus is wired as documented — that needs an I2C scan on the board;
- that the sensor addresses respond — nothing has been read from a sensor;
- that the firmware runs, stays up, or produces valid frames;
- anything about timing, memory use at runtime, or power.

Those are `hardware-validated` claims and none of them are made.
