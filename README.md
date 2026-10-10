<h1 align="center">EdgeSense-AIoT</h1>

<p align="center"><strong>Fault-Aware Adaptive Sensing and IoT Diagnosis Research</strong></p>

<p align="center">Sensor Trust · Cross-Node Evidence · Reproducible Experiments</p>

---

<p align="center">
  <a href="#project-overview">Overview</a> ·
  <a href="#research-method-and-architecture">Method &amp; Architecture</a> ·
  <a href="#implementation-and-verification-status">Status</a> ·
  <a href="#relationship-to-smart-agriculture-edge-ai">Related System</a> ·
  <a href="README.zh-CN.md">简体中文</a>
</p>

---

<a id="project-overview"></a>

EdgeSense-AIoT studies how sensor trust and evidence from multiple channels and nearby nodes can distinguish a sensor fault from a shared environmental change. It targets ESP32-S3 / E220 LoRa networks, with trust-aware sampling and evidence-based, AI-assisted diagnosis as planned extensions.

**Current status: Phase 1A software foundation, verified off-device.** Per-channel validity, sensor availability mapping, node configuration, Protocol v1, and the offline evaluation framework are implemented. The sensor-node firmware builds, but has not been flashed or run on an ESP32-S3; fusion, adaptive sampling, and the diagnosis interface are not yet integrated.

## Research questions

- **Primary:** Can within-node cross-channel context and across-node evidence distinguish a single-node sensor fault from a spatially shared environmental event, compared with single-channel readings? What tradeoff does this introduce in shared-event recall?
- **Secondary:** How does trust-aware adaptive sampling change false alarms, resource use, and shared-event recall?

The study does not assume fusion or trust-aware sampling will improve every case. A localized event affecting only one witness node is outside the primary claim: with two witnesses, it can be indistinguishable from a sensor fault. The dashboard, backend, and AI copilot are planned engineering interfaces, not research contributions.

## Research method and architecture

The frozen design separates three responsibilities:

1. **Node-local sensing and trust:** preserve validity per channel, apply the single-channel baseline, and add cross-channel context. Sampling consumes only local evidence, so it does not require a neighbour's readings.
2. **Gateway fusion:** align node timestamps, form fusion windows, and pair fresh samples before comparing nodes. Sequence-number equality is not time synchronization; partial evidence and insufficient alignment are explicit outcomes.
3. **Evidence and diagnosis:** persist observations and decisions in the backend. The planned dashboard and copilot expose read-only evidence and explanations; these access restrictions are design requirements, not implemented capabilities.

**Planned architecture — the status table below shows which parts currently exist:**

```text
Sensor B ─┐  local: sensing → per-channel validity → SensorTrust
          │         → cross-channel context → adaptive sampling → LoRa
Sensor C ─┤  B/C: same planned firmware, co-located 30–50 cm apart
          ▼
      Gateway A: sessions → time alignment → cross-node fusion → decision
          │
      serial uplink
          ▼
      Backend: persistence + read-only API → Dashboard / AI Copilot
```

Cross-node fusion belongs on Gateway A; the star-topology sensor nodes do not receive neighbour readings. The primary comparison uses fixed dense sampling to separate fusion effects from sampling effects. Adaptive experiments additionally report pairing coverage. See the [frozen design](docs/PHASE_0_2_FINAL_DESIGN_FREEZE.md), [architecture map](docs/engineering/architecture.md), and [experiment protocol](docs/research/experiment_design.md).

## Implementation and verification status

| Capability | Current evidence | Remaining work |
| --- | --- | --- |
| Per-channel validity, availability states, driver/frame mapping | Implemented, host-tested, included in firmware build | Physical sensor validation |
| B/C node roster and configuration parity | Implemented, host-tested, included in firmware build | Two-node hardware verification |
| Protocol v1 codec and stream parser | Implemented, host-tested, included in firmware build | Integration with the physical radio path |
| I²C sensing adapter | Integrated with the vendored sensor layer; build-verified | Wiring checks and real reads under this firmware |
| Offline analysis and EXP-000 | Implemented; checked against a synthetic fixture | Research datasets and evaluated policies |
| LoRa transport, SensorTrust, adaptive sampling | Designed; not integrated into the current firmware | Integration and hardware evaluation |
| Cross-channel context / gateway cross-node fusion | Designed; not implemented | Implementation, alignment tests, paired experiments |
| Backend, dashboard, AI copilot | Designed; not implemented | Engineering implementation and read-only contracts |

The recorded firmware build uses ESP-IDF v5.4.4. **Build success is not hardware validation.** Sensor availability describes whether channels can provide readings; it does not establish their trustworthiness. Hardware configuration is documented but unverified for EdgeSense, and the second SHT30 + BH1750 set remains a recorded prerequisite for two-node experiments. See [build evidence](docs/engineering/build.md), [wiring and hardware prerequisites](docs/hardware/wiring.md), and the [Phase 1A report](OVERNIGHT_BUILD_REPORT.md).

## Evaluation status and boundaries

**No research-performance or EdgeSense hardware results are available yet.** [EXP-000](experiments/EXP-000/results/analysis/report.md) is a synthetic fixture that verifies analysis calculations, not evidence of a working fault/event classifier. Cross-channel, cross-node, alignment, adaptive-sampling, and resource-placement evaluations remain planned.

The evaluation separates `sensor_fault`, `shared_event`, and `localized_event` from network faults and device unavailability. Ground truth stays in `truth/episodes.csv`; only the offline analyzer joins it to `decision_label`. Required truth categories with no samples cause analysis failure rather than a misleading rate. Physical interventions and software injection are reported separately, and claims must link to manifests, truth, raw logs, and generated results.

Runtime RAM, algorithm latency, power, energy, and link reliability have not been measured under EdgeSense. A known firmware image size is a build artifact, not a runtime resource measurement. The planned communication-cost proxy, `(DATA bytes + ACK bytes) × 10 / 9600` seconds, describes UART time, not RF airtime or energy. Prior measurements from related projects do not validate this implementation.

## Quick start and reproduction

No hardware is needed for the offline fixture. Use Python 3.13, as in the existing CI, and a C compiler for optional host checks. From the repository root:

```bash
python3 -m venv .venv
source .venv/bin/activate
python -m pip install -r requirements-dev.txt
python scripts/preflight.py
python experiments/analyze_experiment.py --exp experiments/EXP-000
```

The analyzer writes deterministic reports to `experiments/EXP-000/results/analysis/`. These reproduce fixture calculations, not the planned research experiments.

Optional checks matching the individual non-hardware CI steps:

```bash
python scripts/vendor_manifest.py --verify
python scripts/check_config_parity.py
bash scripts/test_host.sh
python -m pytest
python scripts/static_checks.py
```

`scripts/check_all.py` is the full local sweep, including `--require-upstream` vendor verification. It requires upstream checkouts at the paths and pinned revisions recorded in the manifest; missing or advanced upstream versions fail the freeze check. Missing upstream comparisons are reported by the standalone verifier, and skipped checks must not be treated as passes. See [vendor provenance](docs/vendored/) and the [CI workflow](.github/workflows/checks.yml).

To reproduce the recorded build, use an ESP-IDF v5.4.4 shell and the environment notes in the [build guide](docs/engineering/build.md):

```bash
cd firmware/sensor_node
idf.py build
```

Verify the documented wiring before attempting a hardware run. The current entry point exercises sensing and codec round trips; it does not transmit over LoRa or compute trust/change scores.

## Relationship to Smart-Agriculture-Edge-AI

[Smart-Agriculture-Edge-AI](https://github.com/Sver0411/Smart-Agriculture-Edge-AI) uses EdgeSense's verification ladder and manifest/truth/raw/results separation as design references, as recorded in its [integration report](https://github.com/Sver0411/Smart-Agriculture-Edge-AI/blob/main/docs/V0_3_INTEGRATION_REPORT.md). No EdgeSense business implementation or backend/diagnosis logic was copied into that system. EdgeSense remains an independent sensing-and-diagnosis study without an actuator-control path.

## Limitations and next steps

- Validate wiring and both sensor sets, then demonstrate the physical sensing and LoRa path before making hardware claims.
- Implement channel context, gateway alignment/fusion, and trust-aware sampling; report negative results and shared-event recall tradeoffs alongside aggregate improvements.
- Two co-located witnesses do not resolve all localized-event ambiguities. Multichannel agreement is also not proof of an environmental event: a shared I²C fault can affect several channels.
- Gateway-side and backend-side decisions both follow LoRa reception; their placement comparison concerns serial uplink, processing, resources, and backend availability, not avoiding LoRa latency.
- Field deployment, radio compliance, measured energy, mesh networking, multi-gateway failover, and actuators are outside the current evidence or v1 scope.

## License and third-party rights

**No project license has been chosen.** The project is all rights reserved and is not licensed for reuse; no `LICENSE` file is present.

Vendored files under `firmware/common/vendor/` remain subject to their upstream licences, notices, and usage restrictions. The [provenance records](docs/vendored/) identify sources, pinned versions, intended reuse, and boundaries; this README does not change those rights.
