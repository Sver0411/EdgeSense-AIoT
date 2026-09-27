# EdgeSense

**Fault-Aware Adaptive Sensing and AI-Assisted Diagnosis for Resource-Constrained IoT Networks**

[![checks](https://github.com/Sver0411/EdgeSense-AIoT/actions/workflows/checks.yml/badge.svg)](https://github.com/Sver0411/EdgeSense-AIoT/actions/workflows/checks.yml)

> **Status: Phase 1A software foundation built and verified off-device.**
> Per-channel sensor validity, the availability state mapping, the node roster and
> Protocol v1 exist, pass 530 host assertions and 21 Python tests, and build for
> ESP32-S3. **Nothing has been flashed or measured.** No experiment has produced a
> result, so there are no performance numbers in this README — see the capability
> table below for exactly what is and is not verified.

---

## Research scope

| Type | Question |
|---|---|
| **Primary RQ** | Can fusion of within-node cross-channel evidence and across-node (redundant neighbour) evidence distinguish a **single-node sensor fault** from a **spatially shared environmental event**, compared with single-channel readings, and what trade-off does it introduce in shared-event recall? |
| **Secondary RQ** | Does trust-aware adaptive sampling reduce false alarms and resource use, and what does it change about shared-event recall? |

**Not research contributions**: the AI diagnosis copilot, the dashboard, and the backend. They are the engineering layer.
**Not attempted in v1**: telling a *localized* environmental event apart from a sensor fault — with only two witness nodes these are not separable in principle, so this is stated as a limitation rather than claimed.

## What EdgeSense is, structurally

```
Sensor Node B ─┐   node-local loop: sensing → per-channel validity → SensorTrust
Sensor Node C ─┼─▶  → cross-channel context → local change → adaptive sampling → LoRa
   (co-located, │
    30–50 cm)   │   cross-node fusion does NOT happen here: a star-topology node
               │   has no path to a neighbour's readings
               ▼
          Gateway A   session mgmt · beacon · timestamp alignment · cross-node fusion
                      · final decision · evidence aggregation · link quality
               │
          serial uplink
               ▼
            Backend (FastAPI + PostgreSQL)   read-only API · persistence · observability
               ▼
        Dashboard + read-only Copilot
```

**The copilot is read-only by construction, not by convention**: the tool list has no write operations, the API exposes only `GET`, and the database is written only by the gateway ingest path. A contract test fails if a writing tool ever appears.

## What exists, and how far each part is verified

The verification ladder is used strictly. **A lower rung is never reported as a higher one.**

> `designed` → `implemented` → `host-tested` → `firmware-built` → `hardware-tested` → `hardware-validated` → `experimentally-evaluated`

| Capability | designed | implemented | host-tested | firmware-built | hardware-validated |
|---|:--:|:--:|:--:|:--:|:--:|
| Per-channel validity (frame contract) | ✅ | ✅ | ✅ | ✅ | ❌ |
| Sensor availability state (healthy/degraded/unavailable/retry_wait/recovering) | ✅ | ✅ | ✅ | ✅ | ❌ |
| Driver-report → frame mapping policies | ✅ | ✅ | ✅ | ✅ | ❌ |
| Node roster and configuration parity (B/C symmetry) | ✅ | ✅ | ✅ | ✅ | ❌ |
| Protocol v1 codec + stream parser | ✅ | ✅ | ✅ | ✅ | ❌ |
| I2C bring-up via the vendored sensor layer | ✅ | ✅ | n/a (device-only) | ✅ | ❌ |
| Offline evaluation framework + metric refusals | ✅ | ✅ | ✅ | n/a | n/a |
| EXP-000 framework self-check | ✅ | ✅ | ✅ | n/a | n/a |
| LoRa transport integration | ✅ | ❌ | ❌ | ❌ | ❌ |
| SensorTrust integration | ✅ | ❌ | ❌ | ❌ | ❌ |
| Adaptive sampling (S5/S6) | ✅ | ❌ | ❌ | ❌ | ❌ |
| Cross-channel fusion (B2) | ✅ | ❌ | ❌ | ❌ | ❌ |
| Cross-node fusion (B3, gateway) | ✅ | ❌ | ❌ | ❌ | ❌ |
| Backend, dashboard, copilot | ✅ | ❌ | ❌ | n/a | ❌ |

**No ESP32-S3 has been flashed with this firmware.** The build is verified; the
hardware is not. See `docs/engineering/build.md` for the exact toolchain, artifact
sizes and the one vendored warning, and `docs/hardware/wiring.md` for what is
documented versus verified.

## Evaluation status

| Area | Status |
|---|---|
| Analysis framework correctness | **Verified on a fixture** whose answers were derived by counting (EXP-000) |
| Cross-channel vs single-channel discrimination | **Evaluation planned** (EXP-003) |
| Cross-node discrimination (Primary RQ) | **Evaluation planned** (EXP-005 / EXP-006) |
| Trust-aware adaptive sampling (Secondary RQ) | **Evaluation planned** (EXP-007) |
| Time alignment: offset / jitter / drift | **Evaluation planned** (EXP-004) |
| Gateway-side vs backend-side cost and latency | **Evaluation planned** (EXP-008) |
| Edge algorithm flash / RAM / latency | **Not measured** (the firmware build size is known; runtime resources are not) |
| Energy | **Not measured**. v1 reports a **UART-time proxy** only (`(DATA bytes + ACK bytes) × 10 / 9600` s) — not airtime, not joules |
| Link reliability | **Not measured under EdgeSense.** A single prior point-to-point run observed no packet loss over 600.985 s; that describes that run and nothing more |

## Running the checks

```bash
python3 -m venv .venv && .venv/bin/pip install -r requirements-dev.txt
.venv/bin/python scripts/check_all.py
```

`check_all.py` runs every check that needs no hardware — vendor integrity, config
parity, host C tests, Python tests, the EXP-000 self-check and static checks — and
reports one verdict per check. **A check that cannot run is reported as SKIP, never
as PASS.**

To build the firmware (verify the toolchain note in `docs/engineering/build.md`
first):

```bash
cd firmware/sensor_node && idf.py build
```

## Reading the documentation

| Document | What it is |
|---|---|
| `docs/PHASE_0_2_FINAL_DESIGN_FREEZE.md` | **The frozen design.** Authority for architecture, scope, taxonomy, experiments |
| `docs/PHASE_0_DESIGN_REVIEW.md` | The original audit of the pre-existing projects |
| `docs/PHASE_0_1_ARCHITECTURE_CORRECTION.md` | First correction round (layer placement, scope narrowing) |
| `docs/engineering/design_decisions.md` | **Why** each decision was made, and what would reopen it |
| `docs/engineering/architecture.md` | Layer → module map and per-layer testability conditions |
| `docs/research/experiment_design.md` | Taxonomy, truth schema, pairing algorithm, metric gates |
| `docs/hardware/wiring.md` | Single authoritative wiring source (status: `documented`, **not yet `verified`**) |
| `docs/vendored/` | Provenance of every upstream component, with pinned hashes |

**Conflict priority: Phase 0.2 ＞ Phase 0.1 ＞ Phase 0.**

## Reproducing a claim

```bash
python3 scripts/preflight.py                 # what is missing on this machine
python3 scripts/vendor_manifest.py --verify   # upstream code has not drifted
```

Every number reported anywhere in this project must be traceable to an `experiments/EXP-xxx/` directory containing a `manifest.json`, a `truth/episodes.csv`, immutable raw logs, and machine-generated results. **Numbers are not typed by hand.**

## Claims discipline

- Ground truth lives only in `truth/episodes.csv`. **The running system does not know the experimental truth.**
- Physical faults and software-injected faults are separate label axes and are reported separately.
- A result observed once is written as an observation, never as a guarantee.
- Negative and null results are reported. The primary hypothesis is allowed to fail.
- No deployment or regulatory claim is made. These are laboratory results under the current hardware and configuration.

## License

**No license has been chosen yet.** Absent a licence file, the default applies: all
rights reserved, and nothing here is licensed for reuse.

This is deliberate rather than an oversight — picking a licence is the author's
decision, and an earlier draft of this README referred to a `LICENSE` file that did
not exist. If reuse is intended, add one (MIT and Apache-2.0 are the usual choices
for a portfolio project); until then, treat the code as viewable, not reusable.

Third-party code is unaffected by the above. The vendored files under
`firmware/common/vendor/` remain under their own upstream licences, and each
provenance record in `docs/vendored/` names its source and pinned commit.
