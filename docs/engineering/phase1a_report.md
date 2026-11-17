# Overnight Build Report

**EdgeSense — Phase 1A software foundation, unattended build session**
Date: 2026-09-27 (session ran 03:04 → 03:30 GMT+8)
Report type: engineering execution report. Not a design document.

---

## 1. Executive Summary

**What was actually achieved:** EdgeSense went from a frozen design with no code to a
working software foundation with **530 passing C checks across 6 host suites, 21
passing Python tests, a real ESP32-S3 firmware build, and a verified vendor freeze**.
Every check that can run without hardware now runs, from one command, and passes.

**Three things I want to be unambiguous about:**

1. **No hardware was used, at all.** Nothing was flashed, no sensor was read, no I2C
   scan was performed. Two serial ports had boards attached and I deliberately did
   not touch them (reason in §11).
2. **The firmware build is real and reproducible** — ESP-IDF v5.4.4, target esp32s3,
   app image 236,304 bytes. A build is not a hardware validation and is not reported as one.
3. **Phase 1B did not start** and could not: it needs a second SHT30 + BH1750, which
   is a physical blocker, not a software one.

**What the session found that design review had not:** four real defects, all caught
by the checks rather than by reading code:

| # | Defect | Caught by | Severity |
|---|---|---|---|
| 1 | Config generator emitted channel **enum values** instead of **bit masks**, silently dropping the light channel from the expected set (mask `3` instead of `7` — a number that looks entirely plausible) | C-side B/C symmetry test | **High** — would have made the firmware think it does not measure light |
| 2 | A comment in `edge_sensor_state.h` contained `EXP-*/truth/`, whose `*/` **closed the block comment early**, turning the rest of the comment into code | `-Wall -Wextra -Werror` in the host suite | High — did not compile |
| 3 | A hand-authored truth CSV carried a **UTF-8 BOM**, so the first column name was `\ufeffepisode_id` and the schema rejected a perfectly good file | Python test suite | Medium — real-world failure mode, since humans edit these files |
| 4 | The static checker **failed on its own source**, because a linter necessarily contains the patterns it searches for | Its own first run | Medium — a check that can never pass is worse than no check |

Defects 1 and 3 are the instructive ones: both produced **plausible-looking output
from wrong input**, which is the failure mode this project is built to avoid.

---

## 2. Starting State

At session start the repository was **not a git repository**. It contained 25
documents and 2 scripts, no runtime code:

- Phase 0 audit, Phase 0.1 correction, Phase 0.2 freeze (three design documents)
- five vendored provenance records + `vendor_sources.json` (hand-authored)
- `scripts/vendor_manifest.py`, `scripts/preflight.py`
- entry documents: `design_decisions.md`, `architecture.md`, `wiring.md`,
  `experiment_design.md`, `README.md`
- `experiments/EXP-000-template/`, `.gitignore`, `datasets/{real,injected}/`

Verification at start: `vendor_manifest.py --verify` passed (5 repos, 74 frozen
files). `preflight.py` exited 0 and reported the gaps (§9).

---

## 3. Files Added

**69 files added, 4 modified**, in four commits. Grouped by what they are for:

**Portable C core** (`firmware/common/`)
- `sensor/edge_channel.{h,c}` — per-channel validity frame contract, 3 enforced rules
- `sensor/edge_sensor_state.{h,c}` — node sensor state mapping (5 states)
- `sensor/edge_sensor_map.{h,c}` — driver report → frame, where the sensing policies live
- `config/edge_nodes.{h,c}` — read-only node roster
- `config/edge_config_generated.h` — **generated** from the YAML, do not edit
- `protocol/edge_protocol.{h,c}` — Protocol v1 codec + stream parser
- `config_include.h` — compatibility shim mapping EdgeSense config to the names the
  vendored sensor layer expects

**Device-side glue**
- `sensor_port/edge_sensor_port.{h,c}` — the only unit in the sensing path that
  touches ESP-IDF hardware APIs

**ESP-IDF project**
- `firmware/sensor_node/{CMakeLists.txt,sdkconfig.defaults,main/CMakeLists.txt,main/main.c}`

**Vendored (26 files, byte-identical to upstream)**
- `firmware/common/vendor/adaptivesense/` — 18 files of the sensor layer
- `firmware/common/vendor/eventguard/` — 8 files of the LoRa transport

**Configuration**
- `config/edge_config.yaml` — the single authoritative configuration
- `scripts/generate_config.py`, `scripts/check_config_parity.py`

**Host tests** (`tests/host/`) — `harness.h` + 6 suites (see §7)

**Offline evaluation**
- `experiments/edgeeval/{__init__,schema,metrics}.py`
- `experiments/analyze_experiment.py`
- `experiments/EXP-000/{manifest.json,truth/episodes.csv,results/decisions.csv,results/analysis/*}`
- `tests/python/test_analyze.py`, `pytest.ini`

**Tooling and docs**
- `scripts/{test_host.sh,test_python.sh,check_all.py,static_checks.py}`
- `requirements-dev.txt`, `.github/workflows/checks.yml`
- `docs/engineering/build.md`

## 4. Files Modified

| File | Change |
|---|---|
| `README.md` | Replaced the evaluation-status section with a per-capability verification table and an explicit 7-rung ladder |
| `docs/engineering/design_decisions.md` | Added D-11 … D-18 (eight decisions taken during this session, each with its alternatives and the evidence that they work) |
| `docs/vendored/vendor_sources.json` | Added `vendored_into` mappings for 26 files; added `sensor_bh1750.h` |
| `docs/vendored/vendor_manifest.json` | Regenerated (now also records the EdgeSense copy hash of each vendored file) |
| `scripts/vendor_manifest.py` | Added vendored-copy verification against upstream |

## 5. Files Intentionally Not Modified

**Every upstream repository was left untouched.** Verified by hash, not by intent:

| Repository | Pinned commit | State at session end |
|---|---|---|
| SensorTrust | `fad43a495abd` | unchanged, clean |
| AdaptiveSense | `6e0d086cf14c` | unchanged, clean |
| EventGuard-LoRa | `f7b6e44597ab` | unchanged by me (its own `paper/main.pdf` was already modified before the session) |
| TinyEdgeBench | `485494ef4644` | unchanged, clean |
| adaptive-lora-iot | no git history | data and logs untouched (**5 files hash-verified as preserved read-only**) |

**Nothing was written into any of them.** The `--verify` check now also proves the 26
vendored copies inside EdgeSense are byte-identical to upstream, and that guard was
tested by deliberately corrupting a copy (it failed correctly, then passed after restore).

Two upstream facts that shaped the session:
- **EventGuard-LoRa is actively receiving commits** (five between 02:12 and 02:44 today).
  The freeze set is unaffected — `firmware/common/` has no uncommitted changes and last
  changed 2026-09-26 — but the discipline is explicit: **pin to the recorded commit, never
  track upstream HEAD.**
- **`adaptive-lora-iot` has no git history at all** (its parent repo has zero commits).
  There is no commit to record, so the freeze is content-hash based and after migration
  **EdgeSense's repository becomes its first history**.

## 6. Implementation Completed

| Unit | What it does | Key design point |
|---|---|---|
| `edge_channel` | Frame contract with per-channel validity and a reason per channel | Enforces three rules: a new cycle clears every channel; a non-finite value is never a measurement; a channel cannot be declared invalid *and* OK |
| `edge_sensor_state` | Maps availability + channel coverage to healthy / degraded / unavailable / retry_wait / recovering | A working SHT30 with a dead BH1750 is **degraded**, not "healthy" and not "down" — the distinction the old single flag could not make |
| `edge_sensor_map` | Driver report → frame | Configuration wins over the driver; an unrecognised status is a failure, so a driver bug cannot promote itself into data |
| `edge_nodes` | Node roster from generated config | B and C differ only in `node_id` |
| `edge_protocol` | Protocol v1 codec + stream parser | Two wire invariants (W1, W2) and a parser that survives garbage, truncation, back-to-back frames, unknown version/type |
| `edge_sensor_port` | I2C bring-up via the vendored layer | The only unit touching ESP-IDF hardware APIs; all testable policy lives elsewhere |
| `edgeeval` + `analyze_experiment.py` | Truth × decision analysis | Two refusals: a required category with no windows stops the run; a decision file carrying truth fields is rejected |
| `main.c` | Boot smoke path | Reads a few cycles, logs each channel with its reason, proves the codec roundtrips |

## 7. Tests Run

Every command below was executed. No output in this report is reconstructed.

| Command | Result | Evidence |
|---|---|---|
| `python scripts/vendor_manifest.py --verify` | **PASS** | `5 repos, 75 frozen files unchanged`; 26 vendored copies identical |
| `python scripts/check_config_parity.py` | **PASS** | `header matches YAML, B/C symmetric (B, C), pins match the hardware record, taxonomy phase-0.2` |
| `bash scripts/test_host.sh` | **PASS** | 6 suites, 530 checks, 0 failed (breakdown in §8) |
| `python -m pytest` | **PASS** | `21 passed in 0.11s` |
| `python experiments/analyze_experiment.py --exp experiments/EXP-000` | **PASS** | 20 windows; metrics in §12 |
| `python scripts/static_checks.py` | **PASS** | `0 problems, 0 warning(s)` |
| `python scripts/check_all.py` | **PASS** | `passed 6  failed 0  skipped 0` |
| `idf.py build` (esp32s3) | **PASS** | `BUILD_RC=0`, app image 236,304 bytes (§10) |
| Deliberate negative test: corrupt a vendored file | **Correctly FAILED** | `VENDORED COPY EDITED … no longer matches upstream`, exit 1; passed again after restore |
| Deliberate negative test: zero-denominator metric | **Correctly FAILED** | `FAIL undefined metric: no windows with ground truth for: shared_event …`, exit 1 |

**What was NOT run:** anything requiring a device. No `idf.py flash`, no serial read
from our firmware, no I2C scan, no sensor measurement. No experiment other than the
offline fixture.

## 8. C Compiler Results

**EdgeSense's own sources: zero warnings under `-std=c11 -Wall -Wextra -Werror`**,
`-O1 -g`. The gate runs before any test: 5 unit files (`edge_channel.c`,
`edge_sensor_state.c`, `edge_sensor_map.c`, `edge_nodes.c`, `edge_protocol.c`) are
compiled with `-Werror` first, and only then are the test translation units built.

**Vendored sources: `-std=c11 -Wall -Wextra`, no `-Werror`** — deliberately. Warnings
in code we did not write and must not edit are reported, never silenced with `-Wno-*`.

| Suite | Checks | Failed |
|---|---:|---:|
| `edge_channel` | 131 | 0 |
| `edge_sensor_state` | 36 | 0 |
| `vendor_supervisor_contract` | 100 | 0 |
| `node_config` | 45 | 0 |
| `edge_sensor_map` | 50 | 0 |
| `edge_protocol` | 168 | 0 |
| **total** | **530** | **0** |

A note on what "530 checks" means: it counts assertions, and several are repeated
across masks and call shapes, so it measures thoroughness rather than coverage. It is
not offered as a coverage figure.

`vendor_supervisor_contract` deserves a note: it is EdgeSense's own test of the
vendored supervisor, asserting the specific behaviours we depend on (probe → timeout →
unavailable → retry → recovery, and threshold = 0 never dropping out). Upstream has its
own tests; this file exists so a future re-vendor cannot silently change the contract.

## 9. Python Tests

**21 tests, all passing** (`pytest 9.1.1`, Python 3.13.12 in `.venv`). They cover:

- EXP-000's hand-derived numbers: confusion matrix, per-class precision/recall/F1,
  fault→event, event→fault, false-alarm rate
- determinism: two runs produce byte-identical output
- **refusal 1**: a required category with no windows raises `MissingTruthCategory` and
  the CLI exits 1
- **refusal 2**: a decision file containing `truth_category` is rejected
- **refusal 3**: a decision label outside `{normal, sensor_fault, shared_event}` is rejected
- truth schema rules: end ≤ start, unknown category, duplicate id, injected without
  `injection_method`, physical without `intervention_description`, unexpected column
- BOM tolerance in hand-authored CSVs
- Wilson interval bounds, and an undefined proportion that is `None`, not zero
- matching behaviour: largest overlap wins; a window overlapping nothing is `normal`;
  excluded categories stay out of the primary metrics
- one-way dependency: `edgeeval` imports nothing from firmware or backend, and no
  firmware source mentions `edgeeval`

## 10. ESP-IDF Build

| Item | Value |
|---|---|
| ESP-IDF | **v5.4.4** |
| Compiler | `xtensa-esp-elf-gcc (crosstool-NG esp-14.2.0_20260121) 14.2.0` |
| CMake | 3.30.2 |
| Target | `esp32s3` |
| Result | **`BUILD_RC=0`** |
| App image | `edgesense_sensor_node.bin` = **236,304 bytes** (0x39B10) |
| App partition | 1 MiB, **77% free** |
| Bootloader | 20,928 bytes | 
| Compile warnings | **1**, in vendored `soil_moisture.c` (`atten_name` unused). **None in EdgeSense code.** |

**Obstacle encountered and solved:** the first build attempt produced
`idf.py: command not found`. Root cause was not a missing install: ESP-IDF's Python
environment here is `idf5.4_py3.13_env`, and with the system 3.9 first on `PATH`,
`export.sh` looks for `idf5.4_py3.9_env` and exits before adding `idf.py` to `PATH`.
Putting a 3.13 interpreter first fixed it. This is recorded in `docs/engineering/build.md`
because the symptom actively misleads.

**What the build does not establish:** that the I2C bus is wired as documented, that any
sensor responds, that the firmware runs, or anything about runtime timing, memory or
power. `hardware-validated` is not claimed and would be false.

## 11. Hardware Validation

**Status: not performed. Not partially performed. Nothing was touched.**

| Item | Result |
|---|---|
| Firmware flashed | **no** |
| Serial read from our firmware | **no** |
| I2C scan on any board | **no** |
| Sensor reading taken | **no** |
| Second sensor set installed | **no** — not purchased yet as far as this session can tell |

**Why no board was touched, given two were visible on serial ports.** Two ports
(`cu.usbmodem1101`, `cu.usbmodem1401`) had devices attached and `preflight.py` saw
them. The session rules require confirming which port is which role and not
overwriting a board whose wiring is unverified — and I could not confirm either. The
only installed sensor set is a single SHT30 + BH1750 with no verified mapping to a
specific board, and the wiring document explicitly records those pins as `documented`,
not `verified`. Flashing an unidentified board to gain a claim I would then have to
retract is the exact trade this project refuses. So the ports were left alone.

**The one honest observation available:** two boards were powered and enumerated. That is
all it supports.

## 12. EXP-000

Framework self-check on a fixture whose answers were derived by counting.

```
PASS EXP-000: 20 windows in the primary analysis
     fault->event       0.4000 [0.1176, 0.7693]  (2/5)
     event->fault       0.2000 [0.0362, 0.6245]  (1/5)
     shared-event recall 0.8000 [0.3755, 0.9638] (4/5)
     false alarm rate   0.2000                   (2/10)
```

Confusion matrix (rows truth, columns decision):

| truth \ decision | normal | sensor_fault | shared_event |
|---|---:|---:|---:|
| `sensor_fault` | 0 | 3 | 2 |
| `shared_event` | 0 | 1 | 4 |
| `normal` | 8 | 0 | 2 |

Per-class: `sensor_fault` precision 0.75 / recall 0.60; `shared_event` precision 0.50 /
recall 0.80; `normal` precision 1.00 / recall 0.80. Per-scenario and per-family
breakdowns are produced, and a report is rendered to Markdown.

**These are fixture numbers, not measurements.** They say the analysis code counts
correctly; they say nothing about sensors, faults or the environment.

Outputs: `experiments/EXP-000/results/analysis/{metrics.json,report.md}` — generated, deterministic, committed so the numbers can be checked without a virtual environment.

## 13. Vendor Integrity

```
VENDOR FREEZE CHECK PASSED — 5 repos, 75 frozen files unchanged
  vendored_into_edge_sense=26 identical=26 modified=0 not_yet_copied=0
  preserved read-only (adaptive-lora-iot real data + logs): 5 files
```

The mechanism was extended this session: `--verify` now also compares each **EdgeSense
copy** against its upstream file, so "we do not edit vendored code" is a check rather
than an intention. Proven by tampering: appending a comment to
`vendor/adaptivesense/sensor.h` produced

```
FAIL — AdaptiveSense: VENDORED COPY EDITED: firmware/common/vendor/adaptivesense/sensor.h
       no longer matches upstream firmware/main/sensor.h
```

and exit 1. Restoring the file made it pass again.

## 14. CI Status

`.github/workflows/checks.yml` — a `non-hardware-checks` job (vendor integrity,
config parity, host C tests, Python tests, EXP-000, static checks) and a separate
`firmware-build` job using `espressif/esp-idf-ci-action@v1` with ESP-IDF v5.4.4,
target esp32s3.

**Status at the time of writing: both jobs pass.** Two runs occurred, and the first
one is the interesting one.

**Run 1 — failed, and that was the point.** `non-hardware checks` failed at
`vendor integrity`; every later step was skipped. The cause was a real defect in the
verification script, not in the repository: it asked two different questions as one.

| Question | Where it is answerable |
|---|---|
| Did the pinned upstream checkout drift? | Only on a machine that has those checkouts |
| Is the vendored copy inside EdgeSense still byte-identical? | Anywhere, using the hashes recorded in the manifest |

The runner has no upstream checkouts, so all five were reported as failures. A red
build caused by the environment is noise, and noise trains people to ignore red.

**Run 2 — both jobs pass**, after the script was rewritten so an absent upstream is
reported as *skipped* with an explicit note, while the vendored copies are always
verified. The summary line states how many upstream checkouts were actually verified,
so a green run cannot be read as more than it is. `--require-upstream` restores the
strict behaviour for a complete local sweep, and is what `check_all.py` uses.

**The unplanned benefit:** run 1's `firmware build (esp32s3)` job **succeeded on a
clean Ubuntu runner**. That is independent confirmation the build is not an artefact
of this macOS machine — something a local build could never establish.

The workflow carries the sentence "this step proves the firmware compiles for the
target. Nothing here is flashed, and no hardware claim follows from it." That remains
true of the green run.

## 15. Phase 1A Checklist

| # | Criterion | Status | Basis |
|---|---|---|---|
| A | Sensor abstraction complete and frozen | **PASS** | `edge_channel.h` contract + `edge_sensor_map`; 131 + 50 checks |
| B | Per-channel validity | **PASS** | Independent channels, reasons preserved, no stale values (host-tested) |
| C | Supervisor | **PASS (vendored + mapped)** | Vendored `sensor_supervisor` unchanged; EdgeSense state mapping covers all 5 states; 100 + 36 checks |
| D | Host tests all really executed and passing | **PASS** | 6 suites, 530 checks, 0 failed |
| E | Portable C clean under `-Wall -Wextra -Werror` | **PASS** | Zero warnings in EdgeSense sources, in both host and device builds |
| F | B/C config parity checked automatically | **PASS** | YAML-level symmetry + C-side roster assertion |
| G | ESP-IDF build succeeds | **PASS (build only)** | `BUILD_RC=0`, esp32s3, 236,304-byte image |
| H | Documentation shows layered status | **PASS** | README ladder table; `build.md`; `wiring.md` still shows no `verified` row |

**Phase 1A is complete at the level of software that can be verified without hardware.**
The parts of Phase 1A that inherently need a board — a real I2C scan, a 30-minute
continuous run, an unplug test — are **blocked**, not done, and are listed in §16.

## 16. Remaining Blockers

**Physical blocker (one, and it gates the next phase)**

- **Second SHT30 + BH1750 not present.** Node C cannot measure anything, so Phase 1B
  (dual-node I2C scan and dual-node continuous run) cannot start. `wiring.md` records
  every pin as `documented`; none can become `verified` without a board.

**Environment blockers (workarounds known, recorded, not solved)**

- ESP-IDF needs a 3.13 interpreter first on `PATH`; otherwise `export.sh` fails
  misleadingly. Documented in `build.md`.
- The machine injects a Python shim into interactive shells that breaks build tools;
  every command here ran under `env -i` or with `PYTHONPATH`/`BASH_ENV` unset.
- No `.venv` existed; one was created. Of the five upstream repos, **none** can be
  reproduced locally as-is (AdaptiveSense has no venv, EventGuard's is broken,
  TinyEdgeBench has no pytest). This project's own environment is now self-contained.

**Software blockers (identified, not yet addressed)**

- LoRa transport not integrated into the firmware build (`e220.c`,
  `e220_stream_parser.c` are vendored but not compiled into the device image).
- No gateway project yet.
- Soil channel not wired; config disables it and the port never samples it.

## 17. Known Issues

1. **`CONFIG_AS_SOIL_WET_RAW 1500` is an uncalibrated placeholder.** It exists only so
   the vendored soil module compiles. It is flagged loudly in `config_include.h`, and
   `EDGE_SOIL_REQUIRE_CALIBRATION=1` is the flag that forbids publishing a soil value
   until a real two-point calibration exists. It is still a made-up number in the tree,
   which is exactly the kind of thing this project is supposed to avoid — it is
   tolerated only because the channel is disabled and the value is unreachable.
2. **`config_include.h` is a compatibility shim.** It renames EdgeSense config into the
   `CONFIG_AS_*` names the vendored layer expects, and creates a small derived leaf
   file. It must be re-checked whenever the vendor set is re-pinned.
3. **Protocol frames are fixed-size.** DATA is 28 bytes, and the channel count is
   baked into the layout (a `_Static_assert` guards it). Adding a channel needs a
   protocol version bump, not a silent extension.
4. **The stream parser buffers 28 bytes linearly.** Correct and terminating for
   fixed-size frames; it would need rework for variable-length frames.
5. **`main.c` never transmits and never samples adaptively.** It is a boot smoke path,
   and says so.
6. ~~The CI workflow has never executed.~~ **Resolved during publication:** it ran,
   failed on a real defect in the vendor check, was fixed, and now passes. See §14.
7. **`test_host.sh` links every object into every test binary.** Harmless today; it
   would hide a missing-symbol error in a future suite.
8. **The static checker's exemptions are path-based and manual.** If a checker is
   renamed, the exemption silently stops applying.

## 18. Technical Debt Created Tonight

Stated plainly, because a session that claims to add no debt is not being straight.

| Debt | Why it was accepted | What would clear it |
|---|---|---|
| `config_include.h` shim | The alternative was editing vendored code, which is forbidden | Re-pin the vendor set and re-derive the shim |
| Uncalibrated `CONFIG_AS_SOIL_WET_RAW` | Needed for the vendored soil module to compile | A real two-point calibration, or a stub that removes the module from the build |
| Fixed-size fixed-channel protocol | Fixed lengths are why the parser is simple enough to prove correct | A versioned variable-length frame, when a channel is added |
| 28-byte linear parser buffer | Same reason | A ring buffer if variable-length frames arrive |
| Duplicated pin values in `check_config_parity.py` | Deliberate: it forces a pin change to be a decision, not a typo | Nothing — this one is intentional, recorded here so it is not "fixed" by accident |
| `-Werror` gap on vendored code | Vendored warnings must not be ours to fail on | Upstream fixes `soil_moisture.c` |
| Committed `results/analysis/*` output | Lets the numbers be inspected without a venv | Regenerate in CI and compare instead |

## 19. Claims Allowed

These are safe to put in a README, an application, or an interview, as written.

- "EdgeSense's portable sensor-frame contract enforces per-channel validity, and it is
  unit-tested on the host: 6 C test suites, 530 assertions, zero failures."
- "The sensor node firmware compiles and links for ESP32-S3 with ESP-IDF v5.4.4
  (`xtensa-esp-elf-gcc 14.2.0`), producing a 236,304-byte application image."
- "EdgeSense's own C sources compile clean under `-Wall -Wextra -Werror`."
- "26 files of upstream code are vendored at pinned commits, and a verification script
  proves each copy is still byte-identical to its upstream — demonstrated by tampering."
- "The offline evaluation framework refuses to report a metric whose denominator has no
  ground truth, and refuses a decision file that carries ground-truth fields. Both
  refusals have tests."
- "The analysis output is deterministic: two runs on the same inputs are byte-identical."
- "The experiment framework is verified end-to-end on a fixture with 20 windows whose
  expected confusion matrix and metrics were derived by counting."
- "Ground truth and system decisions are stored separately, and no firmware source
  contains ground-truth vocabulary — checked automatically."
- "CI passes on a clean runner: vendor integrity, config parity, host C tests, Python
  tests, the EXP-000 self-check, static checks, and an ESP-IDF v5.4.4 build for esp32s3."
- "The firmware build is reproducible on Linux as well as macOS — the esp32s3 build job
  succeeds on a clean Ubuntu runner."

## 20. Claims Not Yet Allowed

Listed because each of these is the next step someone would be tempted to write down.

| Claim | Why it is not allowed yet |
|---|---|
| "The sensor node reads temperature, humidity and light on hardware" | Nothing has been flashed or read |
| "Per-channel validity works on the ESP32" | Host-tested and compiled; **never executed on a device** |
| "The I2C wiring is verified" | `wiring.md` lists every pin as `documented`; no I2C scan has run |
| "The firmware runs for 30 minutes without crashing" | Phase 1A's device exit criterion; not attempted |
| "Dual-node deployment works" | Node C has no sensor; no second sensor set exists |
| "Cross-node fusion distinguishes faults from shared events" | Not implemented. The primary research question has no result |
| "Cross-channel evidence adds discriminative information" | Not implemented; EXP-003 not run |
| "Adaptive sampling reduces communication without losing events" | Not implemented; EXP-007 not run |
| "The protocol works over the LoRa link" | The codec is host-tested; no frame has crossed a radio |
| "Link reliability is high / packet loss is zero" | One prior 600.985 s point-to-point observation exists and describes only that run |
| "Energy consumption is reduced" | Current was never measured. Only a UART-time proxy is defined, and it has not been computed |
| "CI passes" | ~~Never run~~ — now true, see §14. Kept here as a reminder that it
  was not true when first written |
| "Suitable for deployment in Japan" | No regulatory or band verification has been done |

## 21. Next Recommended Action

**Verify the sensing path on one board, before anything else.**

Concretely, the single highest-value next step: connect the existing SHT30 and BH1750
to one ESP32-S3, source `export.sh` with a 3.13 interpreter first, then

1. `idf.py -p <PORT> flash monitor` on **one identified board only**, and
2. an I2C scan to confirm `0x44` and `0x23` both respond.

That converts `wiring.md`'s first two rows from `documented` to `verified`, turns
`hardware-built` into `hardware-tested` for the sensing path, and produces the first
real sensor-frame log — which is the input every later phase needs. It requires no
new parts, only an identified board.

**Deliberately second:** order the second SHT30 + BH1750. It is cheap and it unblocks
Phase 1B, but ordering it does not unblock anything this week, whereas verifying the
sensing path on one board does.

**Explicitly not next:** starting cross-channel fusion, the gateway, the backend or
the copilot. Each of them depends on a verified sensing path, and building them first
would only create work to redo.
