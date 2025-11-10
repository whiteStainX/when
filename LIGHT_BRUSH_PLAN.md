# Plan: "Light Brush" Animation

This document provides a detailed, phased implementation plan for the "Light Brush" animation. The goal is to create an organic, expressive visual that resembles abstract painting, where strokes of light are "painted" onto a canvas in response to the music's rhythm, harmony, and texture.

The animation will be contained within a continuous frame, similar to the `SpaceRockAnimation`, to give it the feeling of a piece of art.

## Phase 0: Scaffolding and Basic Structure

**Goal:** Set up the basic class structure and render a single, static stroke on a framed canvas.

### Step 0.1: Create Animation Files

1.  Create `src/animations/light_brush_animation.h` and `src/animations/light_brush_animation.cpp`.
2.  In the header, define the `LightBrushAnimation` class, inheriting from `when::animations::Animation`.
3.  Implement the required virtual methods (`init`, `update`, `render`, etc.) with placeholder logic.
4.  Add the new `.cpp` file to `CMakeLists.txt` and register the animation type as `"LightBrush"` in `animation_manager.cpp`.

### Step 0.2: Define Core Data Structures

-   In `light_brush_animation.h`, define a struct to represent a single "brush stroke" particle.
    ```cpp
    struct StrokeParticle {
        float x, y;         // Current position (normalized 0.0-1.0)
        float vx, vy;       // Current velocity
        float age;
        float lifespan;
        // ... other state like color, thickness, texture ...
    };
    ```
-   Add a `std::vector<StrokeParticle> particles_;` member to the class. The trail of the brush stroke will be rendered by drawing particles from previous frames, which we will implement in a later step. For now, this vector will just hold the "head" of each stroke.

### Step 0.3: Render a Frame and a Single Particle

1.  In `init()`, create the main `ncplane` for the animation.
2.  In `render()`, borrow the frame-drawing logic from `SpaceRockAnimation` to render a continuous bounding box that fills most of the screen. This will be our "canvas."
3.  Inside the `render()` method, hardcode a single `StrokeParticle` and draw it as a character (e.g., `*` or `█`) at its position within the frame.
-   **Validation:** The program displays a single, static character inside a large frame.

## Phase 1: Rhythmic Spawning and Lifecycles

**Goal:** Bring the canvas to life by having strokes appear and disappear in time with the music.

### Step 1.1: Implement Rhythmic Spawning

-   **Feature:** `features.bass_beat`, `features.mid_beat`, `features.beat_strength`
-   **Logic:** In `update()`, check for beat events.
    -   If `features.bass_beat` is true, spawn a new "heavy" particle (which we will later define as having a thick, slow stroke).
    -   If `features.mid_beat` is true, spawn a new "light" particle (thin, fast stroke).
    -   The number of particles spawned can be proportional to `features.beat_strength`.
    -   New particles should be given a random initial position and velocity.
-   **Validation:** Particles pop into existence on the canvas, synchronized with the drum patterns in the music.

### Step 1.2: Implement Particle Lifecycle

-   **Feature:** `features.treble_envelope`
-   **Logic:**
    1.  In the `StrokeParticle` struct, add `age` and `lifespan` members.
    2.  In `update()`, iterate through all particles. Increment their `age` by `delta_time`.
    3.  When spawning a particle, set its `lifespan` based on `features.treble_envelope`. A high treble energy means the "paint" is vibrant and lasts longer. A low treble energy means it's "dull" and fades quickly.
    4.  Remove any particles where `age >= lifespan`.
-   **Validation:** Particles appear on the beat and fade away after a duration controlled by the high-frequency energy of the music.

## Phase 2: Dynamic and Emotional Movement

**Goal:** Move beyond random motion and make the strokes' paths feel intentional and emotionally connected to the music's harmony and texture.

### Step 2.1: Implement Basic Movement and Turbulence

-   **Features:** `features.total_envelope`, `features.spectral_flatness`
-   **Logic:**
    1.  In `update()`, in the loop over all particles, update their position: `p.x += p.vx * dt; p.y += p.vy * dt;`.
    2.  Implement "turbulence." On each frame, add a small random vector to each particle's velocity. The magnitude of this random vector should be scaled by `features.spectral_flatness`.
    3.  The base speed of the particles should be scaled by `features.total_envelope`.
    4.  Add logic to make particles bounce off the inside walls of the frame.
-   **Validation:** Particles now move around the canvas. Their movement is smooth and flowing for tonal music (low flatness) and chaotic/erratic for noisy music (high flatness).

### Step 2.2: Implement Harmonic "Seeking" Behavior

-   **Feature:** `features.chroma`
-   **Logic:** This is the core of the "emotional" movement.
    1.  In `update()`, if `features.chroma_available` is true, define 1-3 "attractor" points on the canvas. The positions of these attractors should be determined by the dominant notes in the `features.chroma` vector. (e.g., map the 12 notes to 12 positions around a circle).
    2.  In the particle update loop, for each particle, calculate a force vector pointing towards the nearest attractor.
    3.  Modify the particle's velocity (`vx`, `vy`) by adding a fraction of this force vector. This will "nudge" the particle's path towards the harmonic centers of the music.
-   **Validation:** The entire collection of strokes will appear to swarm and flow together, shifting their collective direction as the chords and melody of the song change. The movement feels intentional, not random.

## Phase 3: The Painterly Trail

**Goal:** Transform the moving particles into true "brush strokes" by having them leave a fading trail.

### Step 3.1: Implement the Trail Data Structure

-   **Logic:**
    1.  Instead of a `std::vector<StrokeParticle>`, the main data structure will now be `std::vector<BrushStroke>`, where `BrushStroke` is a new class.
    2.  Each `BrushStroke` object will contain a `StrokeParticle` for its "head" and a `std::deque<TrailPoint>` for its tail, where `TrailPoint` just stores an `x`, `y`, and `spawn_time`.
    3.  In the `update()` loop for each `BrushStroke`, add the head's current position to the front of its `trail` deque.

### Step 3.2: Render the Fading Trail

-   **Feature:** `features.treble_envelope` (to control fade time)
-   **Logic:**
    1.  In `render()`, iterate through each `BrushStroke`.
    2.  Then, iterate through the `TrailPoint`s in its `trail` deque.
    3.  For each point, calculate its age (`current_time - point.spawn_time`).
    4.  Map this age to a brightness/color value. A point that was just created should be bright, and it should fade to black as it approaches its lifespan (which is controlled by `treble_envelope`).
    5.  Draw the character for the trail point with this calculated color. Overlapping strokes will naturally blend as their colors are drawn on top of each other.
-   **Validation:** The moving particles now leave beautiful, fading trails behind them, creating the final "abstract painting" effect. The length and brightness of these trails are controlled by the music.
