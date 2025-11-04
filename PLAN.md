# PLAN.md

This document outlines the phased development plan for the "when" music visualizer.

## Phase 1: DSP and Data Flow

**Goal:** Establish a complete data pipeline from audio capture to frequency analysis. Verify that we are correctly processing audio into usable frequency data.

**Status:** ✅ Completed

**Steps:**
- [x] **Implement `DspEngine`:** Create the `dsp.h` and `dsp.cpp` files. This class will be responsible for all signal processing.
- [x] **FFT Initialization:** The `DspEngine` constructor will initialize the `kissfft` configuration based on a given FFT size.
- [x] **Processing Method:** Implement a method, `process_audio(float* samples, size_t count)`, that takes raw audio data, applies a windowing function (e.g., Hann) to reduce spectral leakage, and executes the FFT.
- [x] **Data Storage:** The `DspEngine` will store the resulting frequency bin magnitudes in an internal buffer, accessible via a getter.
- [x] **Integration:** In `main.cpp`, instantiate the `DspEngine`.
- [x] **Pipeline:** In the main loop, fetch raw samples from the `AudioEngine` and pass them to the `DspEngine` for processing.
- [x] **Verification:** In dev mode, print a selection of the raw frequency bin magnitudes to the screen to confirm the audio-to-DSP pipeline is functional.

**Notes:** Integrated `DspEngine` into the main loop, forwarding captured samples and displaying the first few band magnitudes in developer mode for verification.

## Phase 2: Basic Line Rendering

**Goal:** Draw the fundamental lines of the visualization based on frequency data, without occlusion or Braille rendering.

**Steps:**
1.  **Visualizer Interface:** Create `visualizer.h` to define an abstract base class for visualization effects. This promotes modularity.
2.  **`PleasureVisualizer` Class:** Create `pleasure.h` and `pleasure.cpp`. The `PleasureVisualizer` class will inherit from the base `Visualizer`.
3.  **Configuration:** The `PleasureVisualizer` constructor will accept configuration parameters (e.g., number of lines, spacing).
4.  **Initial `render` Method:** Implement the `render` method to:
    a.  Divide the frequency bins from the `DspEngine` among the lines.
    b.  For each line, calculate a single magnitude value (e.g., the average or peak).
    c.  Map this magnitude to a vertical displacement for the line.
    d.  Draw a simple placeholder (e.g., a row of `#` characters) at the correct vertical position for each line.
5.  **Main Loop Integration:** In `main.cpp`, instantiate `PleasureVisualizer` and call its `render` method inside the main loop.

## Phase 2.5: Line Shaping (Peaks and Slopes)

**Goal:** Refactor the rendering logic to calculate a precise, floating-point path for each line, including shaped peaks with randomness. This path will then be rendered using simple placeholder characters as a temporary validation step.

**Steps:**

1.  **Update `PleasureConfig`:** In `src/pleasure.h`, add new parameters to `PleasureConfig` to give us fine-grained control over the line's shape.
    *   `peak_width_percent` (e.g., `0.5f`): A float controlling what percentage of the screen width the audio-reactive peak will occupy.
    *   `randomness_factor` (e.g., `0.2f`): A float controlling how much the peak's center can randomly shift left or right.

2.  **Refactor `PleasureVisualizer::render`:** The core of this phase is to change the rendering logic from a single vertical offset to a per-pixel calculation.
    a.  **Remove Old Logic:** Delete the current code that calculates a single average magnitude and draws a full line with `ncplane_putstr_yx`.
    b.  **Per-Line Calculation:** The main loop will still iterate through each line to be drawn.
    c.  **Calculate Peak Position:** For each line, determine the horizontal range of its audio-reactive peak.
        i.  Calculate a random horizontal offset based on `randomness_factor`.
        ii. Use this offset and `peak_width_percent` to find the `start_x` and `end_x` screen coordinates for the peak.
    d.  **Generate Precise Path:** For each line, iterate through every horizontal column `x` of the screen (`0` to `dimx - 1`):
        i.  **For `x` outside the peak (`x < start_x` or `x > end_x`):** The line is flat. The vertical position is simply its `base_y`.
        ii. **For `x` inside the peak:** Map the frequency bins from the `DspEngine` to this segment of the line. Calculate the displacement based on the audio magnitude at that point. To create a smooth peak instead of a block, apply a shaping function (like a triangle or a smooth bell curve) to the magnitudes across the peak's width. The final vertical position will be `base_y - displacement`.
    e.  **Render Placeholder:** After calculating the precise `y` coordinate for a given `x`, **round it to the nearest integer** and use `ncplane_putchar_yx` to draw a single `#` character at that `(x, rounded_y)` position.

## Phase 3: Braille Rendering

**Goal:** Enhance the visual fidelity by rendering the lines as a series of dots using Unicode Braille characters.

**Steps:**
1.  **Dot Buffer:** In `PleasureVisualizer`, create a 2D buffer (e.g., `std::vector<uint8_t>`) that represents the grid of individual Braille dots for the entire plane.
2.  **Update `render`:** Modify the `render` method. Instead of drawing placeholder characters, calculate the full path of each distorted line. For each point on the path, set the corresponding bit in the Braille dot buffer.
3.  **Braille Composition:** After the dot buffer is populated, iterate through it in 2x4 chunks (the size of a Braille character).
4.  **Character Generation:** For each 2x4 chunk, compose the corresponding Unicode Braille character (`U+2800` to `U+28FF`) based on the dot pattern.
5.  **Screen Update:** Use `ncplane_putwc()` to write the generated Braille characters to the `notcurses` plane.

## Phase 4: Occlusion

**Goal:** Implement the signature "Unknown Pleasures" effect where nearer lines block out farther lines.

**Steps:**
1.  **Skyline Buffer:** In `PleasureVisualizer`, create a 1D "skyline" buffer (an array with size equal to the plane's width). This will track the highest visible dot in each screen column.
2.  **Render Order:** Modify the `render` method to process lines from front-to-back (which corresponds to drawing from the bottom of the screen to the top).
3.  **Visibility Check:** Before setting a dot at `(x, y)` in the Braille buffer, check if `y` is "above" the current value in `skyline[x]`.
4.  **Conditional Drawing:**
    *   If the point is visible, set the bit in the Braille dot buffer and update the skyline: `skyline[x] = y`.
    *   If the point is not visible (i.e., it is occluded by a nearer line), do nothing.

## Phase 5: Refinement and Configuration

**Goal:** Add final polish, randomness, and expose key parameters through the `when.toml` configuration file.

**Steps:**
1.  **Randomness:** Implement horizontal randomness for the line peaks to prevent them from always appearing in the same column.
2.  **Color:** Add the ability to configure the color of the lines.
3.  **Configuration Loading:** Expand `ConfigLoader` and the `PleasureConfig` struct to load all relevant parameters (e.g., line count, color, spacing, FFT smoothing factors, randomness amount) from `when.toml`.
4.  **Apply Config:** Pass the loaded configuration structs to the `DspEngine` and `PleasureVisualizer` upon their construction.
5.  **Cleanup:** Add code comments where necessary, refine the dev overlay, and ensure the application exits cleanly.
