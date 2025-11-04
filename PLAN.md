# Plan: Implementing the "Pleasure" Animation

This document outlines a detailed, step-by-step plan to implement the `PleasureAnimation`, which mimics the iconic "Unknown Pleasures" album art. The plan is designed to be incremental, with each step producing a verifiable result.

## Goal

To create a real-time, audio-responsive, multi-line visualization using Braille characters in `notcurses`, featuring a pseudo-3D occlusion effect where nearer lines block farther ones.

## Naming Conventions

As per the existing project structure, the implementation will adhere to the following naming conventions:

- **File Names**: `snake_case` (e.g., `pleasure_animation.h`).
- **Class/Struct Names**: `PascalCase` (e.g., `PleasureAnimation`).
- **Function/Method Names**: `snake_case` (e.g., `draw_occluded_line`).
- **Variables/Parameters**: `snake_case` (e.g., `skyline_buffer`).

## Prerequisite: Configuration

To ensure we can develop and test the `PleasureAnimation` in isolation, we will configure the application to load only this animation.

- **File to Edit**: `when.toml`
- **Action**: Modify the `scene.animation` key to ensure only the `pleasure` animation is active.

**Example `when.toml` setup:**

```toml
# when.toml

[scene]
animation = "pleasure" # This is the name we will register for the new animation
```

---

## Implementation Steps

### Step 1: Scaffolding the `PleasureAnimation`

- **Goal**: Create the basic class structure for the new animation.
- **Actions**:
  1. Create new files: `src/animations/pleasure_animation.h` and `src/animations/pleasure_animation.cpp`.
  2. In `pleasure_animation.h`, define the `PleasureAnimation` class, inheriting from the base `Animation` interface.
  3. Implement the required virtual methods (`init`, `bind_events`, `update`, `render`) with empty bodies in `pleasure_animation.cpp`.
  4. Add the new source files to the `CMakeLists.txt` to ensure they are compiled.
  5. Register the new animation in the `AnimationManager` so it can be loaded from the configuration.
- **Validation**: The project compiles successfully without errors. When run, the application starts and exits cleanly, showing a blank screen.

### Step 2: Basic Rendering - A Single Static Line

- **Goal**: Verify that `notcurses` is linked and we can draw a basic Braille line.
- **Actions**:
  1. In `PleasureAnimation::render()`, get the main `ncplane`.
  2. Use `ncplane_hline_interp()` or a simple loop with `ncplane_putwc()` to draw a single, straight, horizontal line across the screen using a Braille character (e.g., `U+2840` which is `⠠`).
- **Validation**: When the program is run, a static, horizontal Braille line appears on the screen. This confirms the basic rendering setup.

### Step 3: Drawing a Single, Sloped Line Segment

- **Goal**: Implement the core line-drawing logic on the Braille sub-cell grid.
- **Actions**:
  1. Create a private helper function: `draw_line(ncplane* p, int y1, int x1, int y2, int x2)`.
  2. Implement a Bresenham-like algorithm inside `draw_line` that operates on sub-cell pixel coordinates (i.e., a grid of `rows*4` by `cols*2`).
  3. For each pixel along the calculated line, use `ncplane_putbraille_yx()` to turn on the corresponding dot.
  4. In `render()`, clear the screen and call `draw_line()` with two hardcoded pixel coordinates (e.g., from `(10, 10)` to `(20, 100)`).
- **Validation**: The application displays a single, continuous, sloped line made of Braille dots. This validates the line rasterization algorithm.

### Step 4: Introducing the History Buffer and Audio Reactivity

- **Goal**: Make a single line react to audio and represent it as a time-series waveform.
- **Actions**:
  1. In `pleasure_animation.h`, add a `std::deque<float> history_buffer;` to the class.
  2. In `bind_events()`, subscribe to the `FrameUpdateEvent`.
  3. In the `update()` method (the event handler), calculate an average magnitude from a slice of the incoming FFT data. Push this new magnitude to the front of `history_buffer` and pop from the back to maintain a fixed size.
  4. In `render()`, iterate through the `history_buffer` from `j = 0` to `size-2`. For each pair of consecutive magnitudes, calculate their `(x, y)` pixel coordinates and use the `draw_line()` function from Step 3 to connect them.
- **Validation**: Running the program while playing audio will now show a single, continuous, horizontally scrolling waveform that reacts to the music.

### Step 5: Implementing Multi-Line & The Occlusion `skyline_buffer`

- **Goal**: Draw multiple stacked lines and implement the core occlusion effect.
- **Actions**:
  1. Change the data model from a single `history_buffer` to `std::vector<std::deque<float>> history_buffers`.
  2. In `render()`, create and initialize the `skyline_buffer` (a `std::vector<int>` of size `cols*2`) with the bottom-most pixel row value.
  3. Rename `draw_line` to `draw_occluded_line` and add the `skyline_buffer` as a reference parameter.
  4. **Inside `draw_occluded_line`**, before calling `ncplane_putbraille_yx()`, add the crucial check: `if (y < skyline_buffer[x])`. If true, draw the pixel and then update `skyline_buffer[x] = y`.
  5. In `render()`, implement the main front-to-back loop. Iterate from `line = 0` to `num_lines - 1`, calculating the `base_y` for each and calling `draw_occluded_line` for each segment of its history.
- **Validation**: The application now renders multiple stacked, audio-reactive waveforms. The nearer (lower) lines will correctly hide the parts of the farther (higher) lines that are behind them. This is the primary visual goal.

### Step 6: Configuration via `when.toml`

- **Goal**: Make the animation's properties configurable.
- **Actions**:
  1. In the `PleasureAnimation` constructor or `init` method, read settings from the passed `toml::table`.
  2. Create configurable parameters for `num_lines`, `line_spacing`, `magnitude_scaler`, `history_size`, and the frequency band definitions.
  3. Replace all hardcoded values from the previous steps with these configuration values.
- **Validation**: Modify the values in `when.toml` (e.g., change `num_lines` from 20 to 40). Rerun the application and confirm the animation changes accordingly without needing a recompile.

### Step 7: Final Polish - Horizontal Jitter

- **Goal**: Add the final artistic touch to match the source material.
- **Actions**:
  1. Add a `horizontal_jitter` float parameter to the configuration.
  2. In `render()`, when calculating the `x` coordinate for a line segment's endpoint, add a random offset. The offset's magnitude should be influenced by the audio magnitude and the `horizontal_jitter` config value.
- **Validation**: The peaks of the waveforms will no longer be perfectly aligned vertically. They will have a subtle, random horizontal shift, completing the "Unknown Pleasures" aesthetic.
