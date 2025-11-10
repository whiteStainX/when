# Plan: "Space Rock" Animation

This document outlines a detailed, phased implementation plan for the "Space Rock" animation. The design is inspired by 80s sci-fi aesthetics and the track "云宫迅音," using solid squares within a bounding box to create a geometric, percussive, and musically intelligent visualization.

Detailed desctiption of the animation:

1. Display the animation at the center of the screen just like our pleasure_animation, and follow the DEV_GUIDE framework and strucure
2. A frame as a boudary of the animation, the elements of our animation are just solid filled saures, responding to the music in size, position and also how many in total being displayed
3. The animation is to visulize the 80s sci-fi music, to be specific is 云宫迅音 from 1986 tv series journey to the west

## Phase 0: Scaffolding and Basic Rendering

**Goal:** Set up the basic class structure and render a single, static square within a frame.

### Step 0.1: Create Animation Files

1.  Create `src/animations/space_rock_animation.h` and `src/animations/space_rock_animation.cpp`.
2.  In the header, define the `SpaceRockAnimation` class, inheriting from `when::animations::Animation`.
3.  Implement the required virtual methods (`init`, `update`, `render`, etc.) with empty or placeholder logic.
4.  Add the new `.cpp` file to `CMakeLists.txt` and register the animation type as `"SpaceRock"` in `animation_manager.cpp`.

### Step 0.2: Define Core Data Structures

- In `space_rock_animation.h`, define a struct to represent a single square:
  ```cpp
  struct Square {
      float x, y;         // Position (normalized 0.0-1.0 within the frame)
      float size;         // Size (normalized)
      float velocity_x;
      float velocity_y;
      // ... other state like color, age, etc.
  };
  ```
- Add a `std::vector<Square> squares_;` member to the class to hold all active squares.

### Step 0.3: Render a Bounding Box and a Static Square

1.  In `init()`, create the main `ncplane` for the animation.
2.  In `render()`, calculate the dimensions for a central bounding box (e.g., 80% of the plane width/height). Draw this box using `ncplane_box_sized()`.
3.  Inside the `render()` method, hardcode a single `Square` instance and write a helper function `renderSquare(const Square& s)` that translates its normalized coordinates into plane coordinates and draws a solid block of `█` characters using `ncplane_putstr_yx()`.

- **Validation:** Running the program shows a single, static square inside a bounding box at the center of the screen.

## Phase 1: Rhythmic Spawning and Sizing

**Goal:** Make squares appear and resize in time with the music's core rhythm and energy.

### Step 1.1: Implement Rhythmic Spawning

- **Feature:** `features.bass_beat`
- **Logic:** In `update()`, check `if (features.bass_beat)`. If true, spawn a batch of new squares.
  - Create a helper function `spawnSquares(int count)` that adds new `Square` objects to the `squares_` vector at random initial positions.
  - The number of squares to spawn can be a fixed number for now (e.g., 5), or proportional to `features.beat_strength`.
- **Validation:** Squares appear in bursts, synchronized with the kick drum or bass line of the music.

### Step 1.2: Control Square Lifetime and Count

- **Feature:** `features.bass_envelope`
- **Logic:**
  1.  Add an `age` or `lifespan` member to the `Square` struct. In `update()`, increment the age of all squares and remove any that have exceeded their lifespan.
  2.  Use `features.bass_envelope` to control the maximum number of squares. In `update()`, after removing old squares, check `if (squares_.size() > max_squares)`. If so, remove the oldest squares until the count is below the maximum. The `max_squares` variable should be mapped from `features.bass_envelope` (e.g., `max_squares = 5 + bass_envelope * 50`).
- **Validation:** The screen fills up with squares during loud bass sections and clears out during quiet ones. The visualization feels dense or sparse depending on the music's low-end energy.

### Step 1.3: Implement Dynamic Sizing

- **Feature:** `features.mid_energy_instantaneous` and `features.mid_beat`
- **Logic:**
  1.  When spawning new squares (on a `bass_beat`), set their initial `size` based on `features.mid_energy_instantaneous`. A loud snare hit (`mid_beat` is also true) should result in a noticeably larger square.
  2.  In `update()`, you can optionally add logic to make existing squares "breathe" by slowly interpolating their size towards a target based on `features.mid_envelope`.
- **Validation:** The size of the squares clearly reflects the energy of the melodic and snare elements of the track.

## Phase 2: Dynamic Movement and Texture

**Goal:** Add motion and textural changes to the squares, making the composition feel alive and reactive to the "feel" of the audio.

### Step 2.1: Implement Velocity and Jitter

- **Feature:** `features.treble_energy`
- **Logic:** In `update()`, add a small, random velocity to each square's position, scaled by `features.treble_energy`.
  ```cpp
  // In the loop over all squares in update()
  float jitter_amount = features.treble_energy * params_.max_jitter; // New config param
  square.x += random_between(-jitter_amount, jitter_amount);
  square.y += random_between(-jitter_amount, jitter_amount);
  // Clamp positions to stay within the 0.0-1.0 bounds.
  ```
- **Validation:** The squares shimmer and move around more during sections with lots of hi-hats and cymbals, and become still during quieter parts.

### Step 2.2: Implement Positional Mapping

- **Feature:** `features.spectral_centroid`
- **Logic:** When spawning new squares, use the `spectral_centroid` to influence their initial Y-position.
  - A low centroid (dark sound) should bias the random spawn position towards the bottom half of the bounding box.
  - A high centroid (bright sound) should bias the spawn position towards the top half.
- **Validation:** The entire visual composition moves up and down in response to the "brightness" of the audio, creating a strong visual metaphor.

### Step 2.3 (Advanced): Implement Tempo-Synced Movement

- **Feature:** `features.beat_phase`
- **Logic:** Instead of smooth, continuous movement, make the squares "snap" to new positions in time with the music.
  1.  In the `Square` struct, add `target_x` and `target_y`.
  2.  In `update()`, when a beat is detected (`features.beat_detected`), calculate a new random `target_x` and `target_y` for each square.
  3.  On every frame, smoothly interpolate the square's `x` and `y` towards its target.
  4.  Alternatively, for a more "8-bit" feel, only update the square's actual position `(x, y)` to its `(target_x, target_y)` when `features.beat_phase` wraps around.
- **Validation:** The movement of the squares is no longer random but feels locked to the song's tempo, creating a powerful sense of groove.

## Phase 3: Color and Final Polish

**Goal:** Add a final layer of visual sophistication by making the color palette react to the music's harmony.

### Step 3.1: Implement Harmonic Coloring

- **Feature:** `features.chroma` and `features.chroma_available`
- **Logic:**
  1.  In `update()`, check `if (features.chroma_available)`.
  2.  If true, analyze the `features.chroma` array to find the 1-3 most dominant pitch classes.
  3.  Create a small, dynamic color palette of 2-3 colors based on these notes. (e.g., C -> Red, D -> Orange, E -> Yellow, etc.).
  4.  When spawning new squares, assign them a random color from this dynamic palette.
- **Validation:** The color scheme of the animation shifts in real-time to match the chords and key of the music, creating a true synesthetic effect.

### Step 3.2: Add Fallback Coloring

- **Feature:** `features.spectral_centroid`
- **Logic:** If `features.chroma_available` is false, use `spectral_centroid` as a fallback to control color.
  - Map the 0.0-1.0 range of the centroid to a color gradient (e.g., Blue -> Magenta -> Red).
  - Assign this color to all squares.
- **Validation:** The animation is still visually interesting and reactive even when the advanced chroma analysis is disabled.
