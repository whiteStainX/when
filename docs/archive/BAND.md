# BAND.md — Sprite-Based ASCII/Braille Band Visuals (Notcurses, C++)

This document tracks the implementation blueprint for the **when** band visualizer. It now
matches the state-free direction captured in `BAND_PLAN.md`: each band member renders a single
loop of sprite frames whose playback speed responds directly to live audio features. The audio
and animation systems remain decoupled—the animation consumes `AudioFeatures` snapshots emitted by
the DSP pipeline.

---

## 1) Requirements (clarified and precise)

### 1.1 Visual Concept
- Display **four characters** (Guitarist, Bassist, Drummer, Vocal) arranged in a **2×2 grid of panels** (comic style).
- Each character is rendered using **pre-authored ASCII/Braille sprites**, where each sprite **frame** is a UTF-8 text file.
- Each character owns a **single ordered sequence of frames**. The animation system varies the **playback speed** (frames per
  second) in response to activity, rather than swapping between per-state sprite sets.
- Optional **zoom/spotlight** treatments are deferred; the base delivery animates the same frames for all activity levels.
- Panels show optional **borders** and **titles**; borders are drawn once at init for performance.

### 1.2 Audio-to-Character Mapping (live, mixed signal)
- The animation consumes the existing `AudioFeatures` contract: smoothed & instantaneous band energies (`bass|mid|treble`),
  envelope followers, per-band onset flags, global beat/bar metadata, spectral centroid/flatness, the chroma vector, and the raw
  `band_flux` span supplied by the DSP stage.
- **Instrument heuristics** (live-feasible, no heavy separation):
  - **Drummer**: percussive density inferred from **bass/treble beat flags** and aggregated high-frequency `band_flux`.
  - **Bassist**: low-band envelope level + streaks of bass-band onsets; uses low-frequency `band_flux` as a sustain proxy.
  - **Vocal**: mid-band energy + **low spectral flatness** (tonal) + dominance of one chroma bin when `chroma_available`.
  - **Guitar/Lead**: mid/treble envelope + higher spectral centroid + mid/high `band_flux` for articulation.
- Heuristic outputs feed a continuous **activity score** per member. Scores are smoothed/hysteretic to avoid jitter while
  remaining responsive to fast changes.
- **Beat/Bar phase locking** remains available so cadence changes can align to musical structure, but the core animation only
  adjusts playback speed (no discrete state transitions).

### 1.3 Performance and UX
- **Real time**: target 60 FPS render; expensive analysis already decimated in DSP. Sprite blits use **row-based** writes per
  plane.
- **Memory**: load **all sprite frames at startup**; no I/O during rendering.
- **Config-first**: behavior tuned by TOML (`when.toml`). Frame directories, min/max FPS per member, smoothing constants, and
  layout controls are configurable.
- **Fail-safe**: When feature groups are disabled (e.g., chroma, HPSS), heuristics fall back gracefully to energies and onsets.

---

## 2) High-Level Design (animation side only)

The audio system remains unchanged: it publishes an `AudioFeatures` struct each frame. The **band system** is purely an
`Animation` implementation that **consumes** features and draws to its own planes.

### 2.1 Module Overview

```
+----------------------+                +--------------------+
| BandDirectorAnimation|  owns 4  --->  | PanelController x4 |
+----------+-----------+                +----------+---------+
           |                                       |
           |                           +-----------+-----------+
           |                           | SpriteSequence        |
           |                           | SpritePlayer          |
           |                           | InstrumentHeuristics  |
           |                           | PlaybackConfig        |
           |                           +-----------------------+
           |                                       |
           +-----------------------------+---------+
                                         |
                                         v
                                      ncplane
```

- **BandDirectorAnimation**: lays out the 2×2 grid, draws borders/titles, orchestrates panel activation, and forwards shared
  timing inputs (beat/bar phase) to each panel.
- **PanelController** (per member): owns the sprite sequence, playback controller, heuristic evaluation, and a dedicated
  `ncplane` sized to its panel.
- **SpriteSequence**: ordered `std::vector<SpriteFrame>` loaded from `assets/sprites/<member>/frame_XX.txt` files.
- **SpritePlayer**: advances frames using a variable FPS derived from the member’s activity score and configuration.
- **InstrumentHeuristics**: computes a smoothed `[0,1]` activity score from `FeatureView` input.
- **PlaybackConfig**: per-member tuning (min/max FPS, beat-lock flags, response curves).

All drawing is done with **row writes** (`ncplane_putstr_yx`) to minimize per-cell overhead. Planes are created once and reused.

---

## 3) Class Sketches (C++ headers)

> Namespaces omitted for brevity; adapt to your `when::animations` structure.

```cpp
// Sprite types
struct SpriteFrame {
  int width, height;
  std::vector<std::string> rows; // UTF-8 lines, width chars each (Braille allowed)
};

struct SpriteSequence {
  std::vector<SpriteFrame> frames;
};

// Playback configuration and driver
struct PlaybackConfig {
  float min_fps = 2.0f;
  float max_fps = 12.0f;
  bool beat_lock = false;          // optional: only advance on beat/bar subdivisions
  float smoothing_seconds = 0.12f; // activity low-pass window
};

class SpritePlayer {
public:
  void set_sequence(const SpriteSequence* seq);
  void set_config(const PlaybackConfig* cfg);
  void update(float dt, float activity, float beat_phase, float bar_phase);
  const SpriteFrame& current() const;
  void reset();

private:
  const SpriteSequence* sequence_ = nullptr;
  const PlaybackConfig* config_ = nullptr;
  float accumulator_ = 0.0f;
  std::size_t index_ = 0;
  float smoothed_activity_ = 0.0f;
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
  float activity_score(const FeatureView& f) const; // returns [0..1]

private:
  MemberRole role_;
};

class PanelController {
public:
  PanelController(MemberRole role, PlaybackConfig cfg, SpriteSequence sprites);
  void init(notcurses* nc, int x, int y, int w, int h, bool border);
  void update(const FeatureView& f, float dt);
  void render();
  void set_title(const std::string& t);

private:
  notcurses* nc_{nullptr};
  ncplane* plane_{nullptr};
  int x_, y_, w_, h_;
  bool border_ = true;

  MemberRole role_;
  PlaybackConfig cfg_;
  SpriteSequence sprites_;
  SpritePlayer player_;
  InstrumentHeuristics heur_;
  std::string title_;
};

class BandDirectorAnimation /* : public Animation */ {
public:
  void init(notcurses* nc, const AppConfig& cfg) /*override*/;
  void update(float dt, const AudioMetrics& m, const AudioFeatures& a) /*override*/;
  void render(notcurses* nc) /*override*/;

private:
  struct PanelPos { int x, y, w, h; };
  std::array<PanelController, 4> panels_;
  std::array<PanelPos, 4> layout_;

  void compute_layout(int term_w, int term_h, int padding);
};
```

---

## 4) File/Asset Conventions

```
assets/sprites/
  guitarist/
    frame_01.txt
    frame_02.txt
    ...
  bassist/
  drummer/
  vocal/
```

- **Each `.txt` contains exactly one frame.** The loader enforces consistent width/height across all frames in a directory.
- Frames are sorted **alphabetically** (e.g., `frame_01.txt`, `frame_02.txt`, …) to determine playback order.
- Keep per-frame **row strings** for fast `ncplane_putstr_yx` writes.
- `directory_demo/` remains as a minimal fixture for tests and harnesses.

---

## 5) Heuristics (initial activity curves)

> Tune via TOML; these are safe starting points.

- **Drummer**
  - `activity = clamp01( 0.35*(bass_instant + treble_instant) + 0.3*high_flux + 0.35*(bass_beat || treble_beat ? 1.0 : 0.0) )`
- **Bassist**
  - `activity = clamp01( 0.6*bass_env + 0.25*bass_instant + 0.15*low_flux )`
- **Vocal**
  - `activity = clamp01( 0.45*mid_env + 0.3*(1-flatness) + 0.25*chroma_dominance )`
- **Guitar/Lead**
  - `activity = clamp01( 0.35*mid_env + 0.35*treble_env + 0.3*(centroid_norm + 0.5*mid_flux + 0.5*high_flux) )`

Map each activity value to FPS via linear interpolation: `fps = lerp(min_fps, max_fps, activity)`. Optional beat/bar locking can
quantize frame advances to musical boundaries.

---

## 6) Rendering Details (Notcurses)

- Create **one `ncplane` per panel**; draw borders once (use box-drawing).
- Clear panel with `ncplane_erase(plane_)` only if the new frame has different coverage; otherwise overwrite rows to minimize
  flicker.
- **Row writes**: for each `row` in `SpriteFrame`, `ncplane_putstr_yx(plane_, y0 + r, x0, row.c_str())`.
- Use **Braille** (U+2800..U+28FF) in your sprite art for 2×4 subpixel density. You can mix ASCII and Braille; keep background
  transparent.
- **Timing**: `SpritePlayer` derives instantaneous FPS from the smoothed activity score. If beat locking is enabled, it only
  advances when `beat_phase` crosses configured subdivision thresholds.

---

## 7) Configuration (`when.toml`) Snippet

```toml
[visual.band]
padding = 1
border = true
titles = ["Guitar", "Bass", "Drums", "Vocal"]
assets_root = "assets/sprites"

[visual.band.members.guitar]
sequence_dir = "guitarist"
min_fps = 2.0
max_fps = 10.0
beat_lock = false

# Repeat for bass/drums/vocal with role-appropriate thresholds
```

> **Loader note**: extend `AppConfig` / `AnimationConfig` in `src/config.h` and update `src/config.cpp`
> so `[visual.band]` and the nested member tables populate runtime structures. Without this change the band animation
> configuration will be ignored.

---

## 8) Future Extensions

Phase 4 of `BAND_PLAN.md` reintroduces spotlight choreography and explicit finite state machines if the direct-drive playback
needs more structure. The groundwork above keeps those options open while allowing the current milestone to focus on a
single-frame-sequence pipeline.
