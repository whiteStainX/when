# BAND.md — Sprite‑Based ASCII/Braille Band Visuals (Notcurses, C++)

This document formalizes the “band members in 2×2 comic panels” concept into an implementable plan for your **when** visualizer. It is written to match your current architecture, where **audio analysis** and **animations** are strictly separated. The goal: a robust, low‑latency, state‑driven system that maps live audio features to four sprite‑based ASCII/Braille characters (Guitarist, Bassist, Drummer, Vocal).

---

## 1) Requirements (clarified and precise)

### 1.1 Visual Concept
- Display **four characters** (Guitarist, Bassist, Drummer, Vocal) arranged in a **2×2 grid of panels** (comic style).
- Each character is rendered using **pre-authored ASCII/Braille sprites**, where each sprite **frame** is a UTF‑8 text file.
- Each character supports **animation loops** for distinct **states**:
  - **Idle**: minimal motion, 2–4 frames (loop).
  - **Normal**: regular playing, 4–8 frames (loop).
  - **Fast**: intense playing, 6–12 frames (loop).
  - **Spotlight/Zoom**: a short loop or still(s), possibly **higher‑resolution** or **close‑up** version.
- **Zooms** are implemented by **swapping to a higher‑resolution sprite set** (preferred), optionally with small plane translations for “camera” motion (no runtime scaling of glyphs).
- Panels show optional **borders** and **titles**; borders are drawn once at init for performance.

### 1.2 Audio‑to‑Character Mapping (live, mixed signal)
- The engine consumes **high-level features** (not raw audio): smoothed/instant energies, per-band onsets/flux, LUFS, spectral flatness/centroid/roll-off, optional chroma, optional HPSS split, beat/bar phases.
- **Instrument heuristics** (live-feasible, no heavy separation):
  - **Drummer**: percussive onsets density (bass+treble), HPSS percussive share (if enabled).
  - **Bassist**: low-band envelope level + low-band onset streaks.
  - **Vocal**: mid-band energy + **low spectral flatness** (tonal) + **chroma coherence** (dominant pitch classes).
  - **Guitar/Lead**: mid/treble envelope + higher flatness and/or rising roll-off (brightness).
- **Hysteresis + timers** for all state transitions to avoid flapping in noisy environments.
- **Beat/Bar phase locking**: Spotlight entries occur on **beats** (or bar boundaries) to feel musical.
- **Priority & exclusivity**: when a member enters Spotlight, hold it for `N` beats/bars unless a stronger event claims the focus.

### 1.3 Performance and UX
- **Real time**: target 60 FPS render; expensive analysis already decimated in DSP. Sprite blits use **row-based** writes per plane.
- **Memory**: load **all sprite frames at startup**; no I/O during rendering.
- **Config‑first**: behavior tuned by TOML (`when.toml`). All thresholds, timers, panel layout, and sprite paths are configurable.
- **Fail‑safe**: When feature groups are disabled (e.g., chroma, HPSS), heuristics fall back gracefully to energies and onsets.

---

## 2) High‑Level Design (animation side only)

The audio system remains unchanged: it publishes an `AudioFeatures` struct each frame. The **band system** is purely an **Animation** implementation that **consumes** features and draws to its own planes.

### 2.1 Module Overview

```
+----------------------+
| BandDirectorAnimation|  <-- a single Animation that owns the 2×2 layout
+----------+-----------+
           |
           +-- four PanelControllers (one per member)
                 |
                 +-- SpriteSet (Idle/Normal/Fast/Spotlight)
                 +-- SpritePlayer (frame clock, looping)
                 +-- InstrumentStateMachine (idle/normal/fast/spotlight)
                 +-- Heuristics (features → intents)
                 +-- ncplane (panel surface)
```

- **BandDirectorAnimation**: lays out the 2×2 grid, draws borders/titles, orchestrates panel activation and spotlight transitions.
- **PanelController** (per member): owns the state machine, sprite playback, and a dedicated `ncplane` sized to its panel.
- **SpriteSet**: a collection of preloaded `Frame` arrays: `{idle[], normal[], fast[], spotlight[]}` (+ optional alt‑res versions).
- **SpritePlayer**: advances frames using an internal clock that can **sync to beat/bar phase**.
- **InstrumentStateMachine**: state, thresholds, timers, and transitions with hysteresis.
- **Heuristics**: simple, composable predicates (e.g., `is_percussive_peak()`, `bass_is_dominant()`, `vocal_solo_likelihood()`), fed by `AudioFeatures`.

All drawing is done with **row writes** (`ncplane_putstr_yx`) to minimize per‑cell overhead. Planes are created once and reused.

---

## 3) Class Sketches (C++ headers)

> Namespaces omitted for brevity; adapt to your `when::animations` structure.

```cpp
// Sprite types
struct SpriteFrame {
  int width, height;
  std::vector<std::string> rows; // UTF-8 lines, width chars each (Braille allowed)
};

struct SpriteSet {
  std::vector<SpriteFrame> idle;
  std::vector<SpriteFrame> normal;
  std::vector<SpriteFrame> fast;
  std::vector<SpriteFrame> spotlight;
  // Optional higher-res variants for spotlight:
  std::vector<SpriteFrame> spotlight_hi;
};

// Playback
class SpritePlayer {
public:
  void set_sequence(const std::vector<SpriteFrame>* seq);
  void set_fps(float fps);                // logical fps for this sequence
  void set_phase_lock(bool on);           // if true, advance on beat/bar
  void update(float dt, float beat_phase, float bar_phase);
  const SpriteFrame& current() const;
  void reset();

private:
  const std::vector<SpriteFrame>* seq_ = nullptr;
  float fps_ = 6.0f;
  float acc_ = 0.0f;
  size_t idx_ = 0;
  bool phase_lock_ = false;
};

// States & intents
enum class MemberState { Idle, Normal, Fast, Spotlight };
enum class MemberRole  { Guitar, Bass, Drums, Vocal };

struct MemberConfig {
  // thresholds, timers, fps per state, spotlight min bars, etc.
  float idle_floor = 0.04f;
  float fast_in = 0.65f, fast_out = 0.45f; // metric hysteresis
  float spotlight_score_in = 0.75f, spotlight_score_out = 0.55f;
  float spotlight_min_bars = 1.0f;
  float idle_hold_sec = 0.8f;
  float fast_hold_sec = 0.6f;
  // fps per state
  float fps_idle = 2.0f, fps_normal = 6.0f, fps_fast = 10.0f, fps_spot = 8.0f;
};

// Heuristics input (subset of AudioFeatures cached per frame)
struct FeatureView {
  float bass_env, mid_env, treble_env;
  float flatness, rolloff;
  float beat_phase, bar_phase;
  float percussive_ratio;   // if HPSS available else ~0
  float low_flux, mid_flux, high_flux;
  bool  beat_now;
  bool  chroma_available;
  float chroma_coherence;   // e.g., max bin / sum
  float lufs_short;         // optional for scene gating
};

class InstrumentHeuristics {
public:
  explicit InstrumentHeuristics(MemberRole role);
  // returns [0..1] “activity” and “solo/spotlight” likelihoods
  float activity_score(const FeatureView& f) const;
  float spotlight_score(const FeatureView& f) const;

private:
  MemberRole role_;
};

class InstrumentStateMachine {
public:
  explicit InstrumentStateMachine(const MemberConfig& cfg);
  void update(const FeatureView& f, float dt, float bars_elapsed);
  MemberState state() const;
  void force_spotlight(float bars); // director override
  void end_spotlight();

private:
  MemberConfig cfg_;
  MemberState st_{MemberState::Idle};
  float idle_t_{0.f}, fast_t_{0.f}, spot_bars_{0.f};
  bool spot_locked_{false};
};

class PanelController {
public:
  PanelController(MemberRole role, MemberConfig cfg, SpriteSet sprites);
  void init(notcurses* nc, int x, int y, int w, int h, bool border);
  void update(const FeatureView& f, float dt);
  void render();
  void set_title(const std::string& t);

  // Spotlight orchestration (from director)
  bool is_spotlight() const;
  void request_spotlight();  // tries to elevate to spotlight
  void release_spotlight();

private:
  notcurses* nc_{nullptr};
  ncplane* plane_{nullptr};
  int x_, y_, w_, h_;
  bool border_ = true;

  MemberRole role_;
  MemberConfig cfg_;
  SpriteSet sprites_;
  SpritePlayer player_;
  InstrumentHeuristics heur_;
  InstrumentStateMachine fsm_;
  std::string title_;
};

class BandDirectorAnimation /* : public Animation */ {
public:
  void init(notcurses* nc, const AppConfig& cfg) /*override*/;
  void update(float dt, const AudioMetrics& m, const AudioFeatures& a) /*override*/;
  void render(notcurses* nc) /*override*/;

private:
  // layout
  struct PanelPos { int x, y, w, h; };
  std::array<PanelController, 4> panels_;
  std::array<PanelPos, 4> layout_;

  // spotlight policy
  int current_spot_ = -1;
  float spot_lock_bars_ = 0.f;
  float bar_phase_ = 0.f;
  float bpm_ = 120.f;

  void compute_layout(int term_w, int term_h, int padding);
  void manage_spotlight(const FeatureView views[4], float dt, float bar_phase);
};
```

---

## 4) File/Asset Conventions

```
assets/sprites/
  guitarist/
    idle.txt          # frames separated by blank lines or '---'
    normal.txt
    fast.txt
    spotlight.txt
    spotlight_hi.txt  # optional higher-res close-ups
  bassist/...
  drummer/...
  vocal/...
```

- **Each `.txt`** contains **N frames** concatenated top‑to‑bottom, separated by **one blank line** or a delimiter line `---`. Loader splits and stores each as a `SpriteFrame`.
- All frames in one file share the **same width/height** (per state per member) for simple blitting.
- Keep per‑frame **row strings** for fast `ncplane_putstr_yx` writes.

---

## 5) Heuristics (initial values)

> Tune via TOML; these are safe starting points.

- **Drummer**
  - `activity = clamp01( 0.6*percussive_ratio + 0.2*low_flux + 0.2*high_flux )`
  - Spotlight when: `percussive_ratio > 0.65 && onset_density_last_1s > 5/s`
- **Bassist**
  - `activity = clamp01( 0.7*bass_env + 0.3*low_flux )`
  - Spotlight when: `bass_env > P90(10s) && low_flux > P70(10s)` for ≥ ½ bar
- **Vocal**
  - `activity = clamp01( 0.5*mid_env + 0.3*(1-flatness) + 0.2*chroma_coherence )`
  - Spotlight when: `mid_env high` & `(1-flatness) > 0.7` & `chroma_coherence > 0.5` for ≥ ½ bar
- **Guitar/Lead**
  - `activity = clamp01( 0.4*mid_env + 0.4*treble_env + 0.2*(flatness + rolloff_norm) )`
  - Spotlight when: sustained mid/treble onsets and rising roll-off over 1 bar

**State transitions** (per member):
- **Idle → Normal**: `activity > idle_floor` for `> idle_hold_sec`
- **Normal → Fast**: `activity > fast_in` for `> fast_hold_sec`
- **Fast → Normal**: `activity < fast_out` for `> fast_hold_sec`
- **→ Spotlight**: `spotlight_score > in` and **align to next beat**; hold for `spotlight_min_bars`

---

## 6) Rendering Details (Notcurses)

- Create **one `ncplane` per panel**; draw borders once (use box‑drawing).
- Clear panel with `ncplane_erase(plane_)` only if the new frame has different coverage; else overwrite rows to minimize flicker.
- **Row writes**: for each `row` in `SpriteFrame`, `ncplane_putstr_yx(plane_, y0 + r, x0, row.c_str())`.
- Use **Braille** (U+2800..U+28FF) in your sprite art for 2×4 subpixel density. You can mix ASCII and Braille; keep background transparent.
- **Timing**: `SpritePlayer` either accumulates `dt` at `fps` OR increments frame index at beat subdivisions (`beat_phase` in {0, 0.25, 0.5, 0.75}).

---

## 7) Configuration (`when.toml`) Snippet

```toml
[visual.band]
padding = 1
border = true
titles = ["Guitar", "Bass", "Drums", "Vocal"]
assets_root = "assets/sprites"

[visual.band.members.guitar]
idle_floor = 0.04
fast_in = 0.65
fast_out = 0.45
spotlight_score_in = 0.75
spotlight_score_out = 0.55
spotlight_min_bars = 1.0
fps_idle = 2.0
fps_normal = 6.0
fps_fast = 10.0
fps_spot = 8.0
files = { idle="guitarist/idle.txt", normal="guitarist/normal.txt", fast="guitarist/fast.txt", spotlight="guitarist/spotlight.txt", spotlight_hi="guitarist/spotlight_hi.txt" }

# Repeat for bass/drums/vocal with role-appropriate thresholds
```

---

## 8) Phased Implementation Plan

### Phase 0 — Foundations (1–2 days)
- Implement `SpriteFrame`, `SpriteSet`, `SpritePlayer`, loader for stacked‑frames text files.
- Create a tiny test app to blit sequences to an `ncplane` at fixed fps. Verify row-writing performance and UTF‑8 handling.

### Phase 1 — Single Panel Prototype (1–2 days)
- Implement `InstrumentHeuristics` and `InstrumentStateMachine` with hardcoded thresholds.
- Build `PanelController` for **one** role (e.g., Drummer). Wire to `AudioFeatures`. Use minimal sprites.
- Validate state transitions and beat‑aligned frame stepping.

### Phase 2 — Full 2×2 Layout (1–2 days)
- Implement `BandDirectorAnimation`: panel layout, borders, titles.
- Instantiate four `PanelController`s with role‑specific configs. Confirm per‑panel performance at 60 FPS.

### Phase 3 — Spotlight & Zoom (1–2 days)
- Add spotlight policy (priority, exclusivity, min bar hold). Enter/exit on beat boundaries.
- Provide multi‑resolution sprites for spotlight. Implement gentle plane translation for “camera drift”.

### Phase 4 — Heuristic Tuning & Config (ongoing)
- Move thresholds/timers to `when.toml`. Add per‑member sections.
- Add chroma/HPSS‑aware branches when those features are enabled; fallback when disabled.
- Record short sessions to CSV (feature taps) and tune thresholds offline.

### Phase 5 — Polish & Reliability
- Add **graceful degradation** flags (disable spotlight or lower sprite fps if render budget exceeded).
- Add hotkeys: toggle borders, show per‑panel debug meters (activity, spotlight_score).

---

## 9) Testing Checklist

- UTF‑8 safety with Braille; ensure `setlocale(LC_ALL,"")` before notcurses init.
- Sprite loader: mismatched widths/heights rejected with a clear log.
- Render budget respected at 60 FPS (measure average and p95).
- Spotlight transitions only on beats; no thrashing under noisy input.
- Heuristics degrade gracefully without HPSS/Chroma.

---

## 10) Deliverables

- `src/animations/band_director_animation.{h,cpp}`
- `src/animations/panel_controller.{h,cpp}`
- `src/animations/sprite_loader.{h,cpp}` (and `SpriteFrame`, `SpriteSet`, `SpritePlayer`)
- `assets/sprites/<member>/<state>.txt` sample packs
- `when.toml` updates under `[visual.band]` and `[visual.band.members.*]`

---

## 11) Notes & Tips

- Braille gives **2×4 subpixels per cell**—prefer it for hands/face detail.
- Keep **row strings** contiguous; they’re much faster than per‑cell writes.
- Tie animation cadence to **beat/bar** for musicality; use timers for hysteresis.
- Start simple sprites first; refine art later—logic and timing matter most initially.
