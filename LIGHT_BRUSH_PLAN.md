# Plan: "Light Brush" Animation

This document provides a detailed, phased implementation plan for the "Light Brush" animation. The goal is to create an organic, expressive visual that resembles abstract painting, where strokes of light are "painted" onto a canvas in response to the music's rhythm, harmony, and texture.

The animation will be contained within a continuous frame, similar to the `SpaceRockAnimation`, to give it the feeling of a piece of art.

## Phase 0: Scaffolding and Basic Structure

**Goal:** Set up the basic class structure and render a single, static stroke on a framed canvas.

### Step 0.1: Create Animation Files

1.  Create `src/animations/light_brush_animation.h` and `src/animations/light_brush_animation.cpp`.
2.  In the header, define the `LightBrushAnimation` class, inheriting from `when::animations::Animation`.
3.  Implement the required virtual methods (`init`, `bind_events`, `update`, `render`, etc.) with placeholder logic.
4.  Add the new `.cpp` file to `CMakeLists.txt` and register the animation type as `"LightBrush"` in `animation_manager.cpp`.

> **Note:** `bind_events()` must call `bind_standard_frame_updates()` (or equivalent helper from the guide) so that `update()`
> receives frame callbacks. Document any additional triggers here if the animation later needs them.

### Step 0.2: Define Core Data Structures

- In `light_brush_animation.h`, define a struct to represent a single "brush stroke" particle.
  ```cpp
  struct StrokeParticle {
      float x, y;         // Current position (normalized 0.0-1.0)
      float vx, vy;       // Current velocity
      float age;
      float lifespan;
      // ... other state like color, thickness, texture ...
  };
  ```
- Add a `std::vector<StrokeParticle> particles_;` member to the class. The trail of the brush stroke will be rendered by drawing particles from previous frames, which we will implement in a later step. For now, this vector will just hold the "head" of each stroke.

### Step 0.3: Render a Frame and a Single Particle

1.  In `init()`, create the main `ncplane` for the animation.
2.  In `render()`, borrow the frame-drawing logic from `SpaceRockAnimation` to render a continuous bounding box that fills most of the screen. This will be our "canvas."
3.  Inside the `render()` method, hardcode a single `StrokeParticle` and draw it as a character (e.g., `*` or `█`) at its position within the frame.

- **Validation:** The program displays a single, static character inside a large frame.

## Phase 1: Rhythmic Spawning and Lifecycles

**Goal:** Bring the canvas to life by having strokes appear and disappear in time with the music.

### Step 1.1: Implement Rhythmic Spawning

- **Feature:** `features.bass_beat`, `features.mid_beat`, `features.beat_strength`
- **Logic:** In `update()`, check for beat events.
  - If `features.bass_beat` is true, spawn a new "heavy" particle (which we will later define as having a thick, slow stroke).
  - If `features.mid_beat` is true, spawn a new "light" particle (thin, fast stroke).
  - The number of particles spawned can be proportional to `features.beat_strength`.
  - New particles should be given a random initial position and velocity.
- **Validation:** Particles pop into existence on the canvas, synchronized with the drum patterns in the music.

### Step 1.2: Implement Particle Lifecycle

- **Feature:** `features.treble_envelope`
- **Logic:**
  1.  In the `StrokeParticle` struct, add `age` and `lifespan` members.
  2.  In `update()`, iterate through all particles. Increment their `age` by `delta_time`.
  3.  When spawning a particle, set its `lifespan` based on `features.treble_envelope`. A high treble energy means the "paint" is vibrant and lasts longer. A low treble energy means it's "dull" and fades quickly.
  4.  Remove any particles where `age >= lifespan`.
- **Validation:** Particles appear on the beat and fade away after a duration controlled by the high-frequency energy of the music.

> **Implementation Note:** The effectiveness of this animation is highly dependent on the frequency of beat detection events (`bass_beat`, `mid_beat`, etc.). If strokes appear too infrequently, it may be necessary to tune the audio engine's beat detection parameters. Lowering `band_onset_sensitivity` in `feature_extractor.h` will make the detector more sensitive and cause more strokes to be spawned.

## Phase 2: Dynamic and Emotional Movement

**Goal:** Move beyond random motion and make the strokes' paths feel intentional and emotionally connected to the music's harmony and texture.

### Step 2.1: Implement Basic Movement and Turbulence

- **Features:** `features.total_energy`, `features.spectral_flatness`
- **Logic:**
  1.  In `update()`, in the loop over all particles, update their position: `p.x += p.vx * dt; p.y += p.vy * dt;`.
  2.  Implement "turbulence." On each frame, add a small random vector to each particle's velocity. The magnitude of this random vector should be scaled by `features.spectral_flatness`.
  3.  Scale the base speed of the particles by the smoothed energy in `features.total_energy` (or `total_energy_instantaneous` if a snappier response is desired). Document the chosen mapping so implementers know how energy translates into speed multipliers.
  4.  Add logic to make particles bounce off the inside walls of the frame.
- **Validation:** Particles now move around the canvas. Their movement is smooth and flowing for tonal music (low flatness) and chaotic/erratic for noisy music (high flatness).

### Step 2.2: Implement Harmonic "Seeking" Behavior

- **Feature:** `features.chroma`
- **Logic:** This is the core of the "emotional" movement.
  1.  In `update()`, if `features.chroma_available` is true, define 1-3 "attractor" points on the canvas. The positions of these attractors should be determined by the dominant notes in the `features.chroma` vector. (e.g., map the 12 notes to 12 positions around a circle).
  2.  In the particle update loop, for each particle, calculate a force vector pointing towards the nearest attractor.
  3.  Modify the particle's velocity (`vx`, `vy`) by adding a fraction of this force vector. This will "nudge" the particle's path towards the harmonic centers of the music.
- **Validation:** The entire collection of strokes will appear to swarm and flow together, shifting their collective direction as the chords and melody of the song change. The movement feels intentional, not random.

## Phase 3: The Painterly Trail

**Goal:** Transform the moving particles into true "brush strokes" by having them leave a fading trail.

### Step 3.1: Implement the Trail Data Structure

- **Logic:**
  1.  Instead of a `std::vector<StrokeParticle>`, the main data structure will now be `std::vector<BrushStroke>`, where `BrushStroke` is a new class.
  2.  Each `BrushStroke` object will contain a `StrokeParticle` for its "head" and a `std::deque<TrailPoint>` for its tail, where `TrailPoint` just stores an `x`, `y`, and `spawn_time`.
  3.  In the `update()` loop for each `BrushStroke`, add the head's current position to the front of its `trail` deque.

### Step 3.2: Render the Fading Trail

- **Feature:** `features.treble_envelope` (to control fade time)
- **Logic:**
  1.  In `render()`, iterate through each `BrushStroke`.
  2.  Then, iterate through the `TrailPoint`s in its `trail` deque.
  3.  For each point, calculate its age (`current_time - point.spawn_time`).
  4.  Map this age to a brightness/color value. A point that was just created should be bright, and it should fade to black as it approaches its lifespan (which is controlled by `treble_envelope`).
  5.  Draw the character for the trail point with this calculated color. Overlapping strokes will naturally blend as their colors are drawn on top of each other.
- **Validation:** The moving particles now leave beautiful, fading trails behind them, creating the final "abstract painting" effect. The length and brightness of these trails are controlled by the music.

## Phase 4: Expressive Single-Stroke Presentation

**Goal:** Rework the spawning, fading, and rendering rules so the canvas always features a single, expressive brush stroke that lingers and fades organically with the music.

### Step 4.1: Enforce Single-Stroke Lifecycle with Smooth Fading

- **Logic:**
  1.  Update `LightBrushAnimation::update` so the animation maintains at most one active `BrushStroke` at a time. Reuse the existing stroke while it is fading instead of spawning new ones indiscriminately.
  2.  Replace the hard cut-off removal with brightness-based fading. Keep the stroke alive until its computed brightness reaches zero, and drive this brightness from a time-based fade curve (e.g., exponential or eased polynomial) tied to `stroke.head.age / stroke.head.lifespan`.
  3.  Adjust the trail update logic so both the head and tail apply the same fade curve, allowing the visible stroke to dissipate gracefully rather than disappearing abruptly.
4.  Gate new stroke creation on the previous stroke’s fade completion. Once the brightness reaches zero, spawn a fresh stroke based on the next rhythm cue to preserve the “one artist stroke at a time” aesthetic. Flag this gating logic in the implementation notes so it can be relaxed in Phase 5 when multi-stroke layering arrives.
5.  Validate by stepping through an audio-driven run and confirming that a new stroke is never generated until the current stroke has fully faded from the canvas. Annotate the validation checklist to remind implementers that Phase 5 will introduce a stroke pool that supersedes this single-stroke guard.

### Step 4.2: Introduce Braille-Based Organic Thickness

- **Features:** `features.beat_strength`, `features.spectral_flatness` (or other expressive metrics available in the audio pipeline).
- **Logic:**
  1.  Extend `BrushStroke` (and its `TrailPoint`s) with an `intensity` or `thickness` parameter derived from the chosen audio features so stroke boldness mirrors the music.
  2.  Replace single-block rendering with a Braille-dot routine inspired by `PleasureAnimation`, mapping normalized coordinates into the 2×4 Braille cell space and populating dot patterns that reflect the stroke’s intensity.
  3.  Scale the number of lit Braille dots and their spread by the stroke’s current thickness to produce organic, variable-weight strokes. Ensure adjacent samples blend smoothly to avoid rigid, pixelated edges.
  4.  Allow intensity to taper along the trail as the stroke fades, so older trail samples naturally become thinner and lighter before vanishing.
  5.  Validate by rendering sequences with different musical dynamics, confirming that strong beats yield bold, expressive strokes while quieter passages produce thinner, delicate marks.

## Phase 5: Emergent Layered Composition

**Goal:** Evolve the animation into a true abstract painting by allowing an arbitrary number of strokes to coexist, overlap, and fade independently. The visual density and complexity of the canvas will become an *emergent property* of the music's intensity and rhythm, rather than being controlled by an explicit cap.

**Core Philosophy:** We will not manage a "population." We will only define the rules for a stroke's birth, its life, and its death. The complexity of the final image will arise naturally from these simple, local rules.

### Step 5.1: Implement an Uncapped, Emergent Lifecycle

-   **Logic:**
    1.  **Remove `max_strokes`:** Eliminate all logic related to a maximum stroke count. The system no longer needs to know or care how many strokes are active.
    2.  **"Fire-and-Forget" Spawning:** The spawning logic in `update()` remains the same (triggered by `bass_beat`, `mid_beat`, etc.), but it will now simply add new `BrushStroke` objects to the `strokes_` vector without any checks on the current population size.
    3.  **Self-Contained Update:** Each `BrushStroke` object will manage its own state. Its `update(delta_time, features)` method will handle its movement, aging, and fading. This method should return a boolean, `is_alive()`, which is `false` only when the stroke's brightness has faded below a near-zero threshold.
    4.  **Efficient Cleanup:** After iterating through and updating all strokes, use the C++ **erase-remove idiom** to purge all "dead" strokes from the main `strokes_` vector in a single, efficient operation.
        ```cpp
        // In LightBrushAnimation::update(), after the main update loop:
        strokes_.erase(
            std::remove_if(strokes_.begin(), strokes_.end(),
                [](const BrushStroke& s) { return !s.is_alive(); }
            ),
            strokes_.end()
        );
        ```
-   **Validation:** The number of strokes on screen directly reflects the musical activity. A flurry of drum hits creates a dense, chaotic canvas, which then gracefully fades during a quiet passage. The system is stable and performance does not degrade due to the cleanup logic.

### Step 5.2: Implement a Layer-Friendly Rendering Pipeline with Blending

-   **Goal:** To make overlapping strokes look beautiful and luminous, not just like one overwriting another.
-   **Logic:**
    1.  **Off-screen Accumulation Buffer:** In your `LightBrushAnimation` class, create a private member `std::vector<Color> accumulation_buffer_`, where `Color` is a simple struct like `{ float r, g, b; }`. This buffer should be resized to match the `ncplane`'s dimensions (`rows * cols`).
    2.  **Rendering Process:**
        a.  On each `render()` call, clear this buffer to black (`{0,0,0}`).
        b.  Iterate through every `BrushStroke` and every `TrailPoint` in its tail.
        c.  For each point, calculate its current color and brightness.
        d.  **Instead of drawing to the `ncplane`**, find the corresponding index in your `accumulation_buffer_` and **add** the color to it (`buffer[i].r += point.r`). This is **additive blending**.
        e.g.  After all strokes and trails have been processed, iterate through your `accumulation_buffer_`. For each cell, clamp the final color values (e.g., `r = std::min(1.0f, buffer[i].r)`) and then draw a single character to the `ncplane` with that final, blended color.
-   **Validation:** Where strokes cross, the color is visibly brighter and more intense, creating a luminous, layered effect. The performance should be benchmarked; if it's too slow, a fallback to direct rendering can be considered, but the accumulation buffer is key to the high-quality look.

### Step 5.3: Implement Expressive Stroke Geometry

-   **Goal:** Make each stroke's shape and path tell a story based on the music that created it.
-   **Logic:**
    1.  **Evolving Thickness:** When a `BrushStroke` is spawned, its initial thickness can be set by `features.mid_energy_instantaneous`. As it ages, this thickness can decay, or it can "breathe" in time with `features.beat_phase`. This logic lives inside the `BrushStroke::update()` method.
    2.  **Gestural Path Length:** The `lifespan` of a stroke, which is already seeded by `treble_envelope`, now directly controls its potential path length. A long lifespan (from a sustained synth pad) will allow a stroke to draw a long, sweeping gesture across the canvas. A short lifespan (from a percussive hit) will create a short, staccato mark.
    3.  **Variable Curvature:** Introduce a `curvature` property to the `BrushStroke`'s state. This can be set at spawn time based on `features.spectral_centroid` (e.g., brighter sounds create straighter lines, darker sounds create more curved paths). The `update()` method will then use this property to apply a constant turning force to the stroke's velocity, creating elegant arcs instead of just straight lines.
-   **Validation:** The canvas is filled with a variety of strokes: long, graceful arcs mix with short, sharp jabs. The thickness of the lines visibly swells and shrinks with the music's intensity. The overall impression is one of dynamic, intentional, and varied mark-making.

