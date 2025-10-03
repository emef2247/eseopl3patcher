# YMFM Integration Plan (Analysis-first)

Purpose
- Use the YMFM emulator core as an “analysis engine” rather than a real-time audio backend, to observe internal FM/EG state that is otherwise invisible from register write streams.
- Automatically infer per-pattern/channel Gate timing for OPLL→OPL3 conversion without relying on precomputed CSV, and provide a solid foundation for future accuracy improvements.

Scope (Phase C focus)
- Chip: YM2413 (OPLL) first. Keep design extensible to other FM chips.
- Mode: Offline analysis. No real-time constraint; we can afford heavier simulation.
- Output: Channel-level Gate recommendations; optional future per-section/per-note gate variation.
- Deliverables: Minimal YMFM vendoring + C bridge, analysis utilities, integration hooks, CLI options, docs, and tests.

Why YMFM
- License: BSD-3-Clause (compatible with MIT; embed-friendly).
- Breadth: Covers OPLL/OPL/OPN/OPM families → future extensibility.
- Maintainability: Modern, actively maintained, consistent internal model.

What we want to observe (from the emulator)
- EG (envelope generator)
  - Phase: Attack/Decay/Sustain/Release per operator
  - Level: Instantaneous internal envelope level (convertible to dB)
  - Effective rates: AR/DR/RR as influenced by KSR/BLK
- KeyOn/KeyOff propagation
  - Delay and effect on operators/channels after KO bit changes
  - Residual tail level at the next note-on time
- LFO (AM/FM)
  - Effective modulation state if enabled (optional)
- Two-operator interaction (minimum: separate EG levels per op; future: feedback impact)
- Time-derived metrics
  - Tail dB at t_next_on for a hypothetical t_off
  - Silent gap length: (t_next_on - t_off) - min_audible_tail
  - Aggregate score: combine overlap/gap/regularization

Architecture Overview
- Vendor YMFM (subset) into third_party/ymfm (or similar)
- Add an “analysis bridge” (C-callable API) that:
  - Initializes chip with clock/sample rate
  - Feeds register writes in time
  - Steps internal simulation by N samples
  - Exposes safe getters for EG phase/level, channel/operator state
- Integrate with OPLL wrapper
  - When gates.csv is absent or disabled, run the analysis-based auto gate estimator
  - Priority: explicit CSV > auto-estimator (YMFM) > default gate

High-Level Data Flow
1) Parse input VGM → existing pipeline produces a timeline of OPLL writes and derived note-on boundaries.
2) For each channel, collect note-on times (and optional pattern segmentation).
3) For a set of gate candidates, hypothesize t_off = t_on + g*(t_next_on - t_on).
4) Simulate with YMFM from KeyOff to t_next_on, record EG tail at next on.
5) Score each candidate (overlap penalty, gap penalty). Pick best g.
6) Use best g for KEYOFF scheduling during OPLL→OPL3 conversion. Optionally cache to dist/gates_auto.csv.

C/C++ API Sketch (Bridge)
- C++ side (YMFM wrapper), compiled as C++ with extern "C"
```cpp
extern "C" {
// Lifetime
void* ym2413_new(uint32_t clock_hz, uint32_t sample_rate);
void  ym2413_free(void*);

// Register I/O
void  ym2413_write(void*, uint32_t addr, uint8_t data);

// Time stepping
void  ym2413_step(void*, uint32_t n_samples);

// Analysis getters (enabled under YMFM_ANALYSIS)
int   ym2413_get_env_phase(void*, int ch, int op);   // 0=A,1=D,2=S,3=R
int   ym2413_get_env_level(void*, int ch, int op);   // raw internal level
float ym2413_level_to_db(void*, int raw_level);      // helper (optional)
int   ym2413_get_effective_rates(void*, int ch, int op, int* ar, int* dr, int* rr);
}
```

- C side (eseopl3patcher)
```c
typedef struct {
  float best_gate;  // 0..1
  float best_score;
} GateAutoResult;

GateAutoResult gate_auto_estimate_channel_ymfm(
  const NoteOns* ons,               // per-channel note-on timestamps (ascending)
  const OpllEgParams* eg,           // if needed to prime init; YMFM internal state is source of truth
  const float* gate_candidates, int n_candidates,
  YmfmCtx* ctx,                     // chip instance, reused across notes
  float sample_rate, float min_gap_sec,
  float overlap_tail_db_threshold   // e.g., -40 dB
);
```

Gate Estimation Algorithm (YMFM backend)
- Candidate gates: configurable grid (e.g., 0.60–0.95 step 0.05) or golden-section search
- For each note i (except last):
  - Let t_on = ons[i], t_next = ons[i+1], ioi = t_next - t_on
  - For a candidate g:
    - t_off = t_on + g*ioi
    - Emit KeyOff at t_off (if needed), then step until t_next
    - Measure tail_db at t_next (carrier operator focus)
    - gap_sec = max(0, (t_next - t_off) - min_gap_sec)
  - Score_i(g) = 1.0 - w1*overlap_penalty(tail_db) - w2*gap_penalty(gap_sec)
- Aggregate score = mean_i Score_i(g)
- Pick g with maximum aggregate score
- Regularization (optional): discourage extreme g near 0 or 1

Scoring Details (initial)
- overlap_penalty(tail_db):
  - If tail_db > threshold_db (e.g., -40 dB), penalty = (tail_db - threshold_db) / margin
  - Else 0
- gap_penalty(gap_sec):
  - Linear or softplus against target max gap (e.g., >10 ms)
- Weights:
  - Start with w1=1.0, w2=0.5; tune on regression set

CLI/Config
- --gate-auto ymfm|off         default: ymfm if CSV absent
- --gate-grid "0.65,0.7,0.75,0.8,0.85,0.9"
- --gate-search grid|golden    default: grid
- --gate-tail-thresh-db -40
- --gate-min-gap-ms 10
- --gate-cache dist/gates_auto.csv
- --gate-metrics logs/gate_metrics.csv
- Environment variable mirrors (optional) for batch ops

Integration Points
- OPLL wrapper init:
  - If ESEOPL3_GATES_CSV set and valid → use CSV
  - Else if --gate-auto=ymfm → run estimator and cache results
  - Else fallback gate = 0.80
- During conversion:
  - Use best_gate[ch] to compute per-note t_off
- Logging:
  - Emit chosen gate per channel and aggregated score
  - Optional CSV metrics (per-note tail_db, gap_sec, score components)

Performance & Concurrency
- Offline context allows:
  - Candidate×note simulation loops
  - Channel-level parallelism (thread pool)
  - Candidate-level parallelism (smaller benefit due to shared state resets)
- Optimization:
  - Reuse YMFM instance per channel; checkpoint/restore around KO events if needed
  - Reduce sample stepping by event-driven advance (still in sample-accurate increments if required)

Licensing & Third-Party
- YMFM: BSD-3-Clause (compatible)
- Add docs/THIRD_PARTY_NOTICES.md with:
  - Source URL
  - Copyright
  - License text excerpt/notice
- Build guard:
  - USE_YMFM=1 to enable; default ON for analysis builds

Testing & Validation
- Unit:
  - Gate estimator returns stable best_gate for deterministic inputs
  - Getter APIs return consistent EG phase/levels across known scenarios
- Regression (AB):
  - Compare gates.csv (precomputed) vs YMFM-auto on known tracks
  - Objective metrics: overlap ratio > threshold, gap ratio within bounds
  - Subjective: “musical phrasing” parity or improvement
- Fallback:
  - Absence of YMFM (build flag OFF) still builds; CSV path or default gate works

Milestones (Incremental PRs)
- C1: Minimal YMFM vendoring + bridge + fixed-gate auto estimation (per-channel)
  - Features: grid search, cache/metrics, wrapper integration, docs
- C2: Faster/stricter evaluation
  - Golden-section search; channel parallelization; improved scoring model
- C3: Variable gate (by section/note class)
  - Heuristics based on IOI / tempo / pitch bands; optional smoothing
- C4: Deeper interactions
  - Feedback and LFO-aware scoring; optional alternative cores (Nuked) for AB

Open Questions / Risks
- EG Readout:
  - YMFM internal fields exposure: prefer minimal, guarded getters; upstream divergence management
- Timebase Consistency:
  - Sample rate, chip clock, and VGM wait-unit coherence
- Rhythm/Drum modes & special channels:
  - Channel subset handling and percussive behavior in OPLL
- Multi-note overlap:
  - Legato/overlap patterns and KO scheduling strategy (advance KO vs truncate)

Appendix: Example Workflow
1) Build with YMFM
   - make USE_YMFM=1
2) Run without CSV
   - bin/eseopl3patcher --gate-auto ymfm --gate-grid "0.65,0.7,0.75,0.8,0.85,0.9" --gate-cache dist/gates_auto.csv input.vgm -o output.vgm
3) Inspect logs/metrics
   - tail logs/gate_metrics.csv
4) Iterate weights/thresholds as needed

Notes for Future Me (備忘録)
- Always keep priority: explicit CSV > YMFM auto > default
- Persist auto results to dist/gates_auto.csv for reproducibility
- Record tail_db distributions to tune thresholds and weights
- Keep YMFM patches minimal and documented for easy rebase