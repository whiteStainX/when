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
- The animation consumes the **existing `AudioFeatures` contract**: smoothed & instantaneous band energies (`bass|mid|treble`), envelope followers, per-band onset flags, global beat/bar metadata, spectral centroid/flatness, the chroma vector, and the raw `band_flux` span supplied by the DSP stage. No additional LUFS, roll-off, or HPSS outputs are required up-front.
- **Instrument heuristics** (live-feasible, no heavy separation):
  - **Drummer**: percussive density inferred from **bass/treble beat flags** and aggregated high-frequency `band_flux`.
  - **Bassist**: low-band envelope level + streaks of bass-band onsets; uses low-frequency `band_flux` as a sustain proxy.
  - **Vocal**: mid-band energy + **low spectral flatness** (tonal) + dominance of one chroma bin when `chroma_available`.
  - **Guitar/Lead**: mid/treble envelope + higher spectral centroid + mid/high `band_flux` for articulation.
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
  float bass_instant, mid_instant, treble_instant;
  float total_energy;
  float flatness, centroid_norm; // centroid normalized against expected min/max
  float beat_phase, bar_phase;
  float low_flux, mid_flux, high_flux; // aggregated from AudioFeatures::band_flux spans
  bool  beat_now;
  bool  bass_beat, mid_beat, treble_beat;
  bool  chroma_available;
  float chroma_dominance;   // max(chroma) / sum(chroma) when available
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
  - `activity = clamp01( 0.35*(bass_instant + treble_instant) + 0.3*high_flux + 0.35*(bass_beat || treble_beat ? 1.0 : 0.0) )`
  - Spotlight when: `(bass_beat || treble_beat)` fires ≥4 times in the last bar **and** `high_flux > 0.6`.
- **Bassist**
  - `activity = clamp01( 0.6*bass_env + 0.25*bass_instant + 0.15*low_flux )`
  - Spotlight when: `bass_env` exceeds its rolling 8‑bar median by 40% for ≥½ bar and `low_flux > 0.55`.
- **Vocal**
  - `activity = clamp01( 0.45*mid_env + 0.3*(1-flatness) + 0.25*chroma_dominance )`
  - Spotlight when: `(1-flatness) > 0.7`, `chroma_dominance > 0.5` (or `mid_env > 0.75` when chroma disabled) for ≥½ bar.
- **Guitar/Lead**
  - `activity = clamp01( 0.35*mid_env + 0.35*treble_env + 0.3*(centroid_norm + 0.5*mid_flux + 0.5*high_flux) )`
  - Spotlight when: `mid_flux` and `high_flux` stay above 0.6 for a full bar and `centroid_norm` trends upward.

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

> **Loader note**: extend `AppConfig` / `AnimationConfig` in `src/config.h` and update `src/config.cpp`
> so `[visual.band]` and the nested member tables populate runtime structures. Without this change
> the band animation configuration will be ignored.

---

