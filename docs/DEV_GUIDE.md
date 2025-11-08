# Developer's Guide: Creating New Animations

Welcome, creator! This guide provides a standardized framework for developing new animations for the `when` audio visualizer. By following these principles, you can focus entirely on the creative and visual aspects of your animation, while the core engine handles all the complex audio processing.

Our architecture is **service-oriented and event-driven**. The audio engine acts as a "service" that analyzes the music and publishes a rich set of high-level `AudioFeatures`. Your animation acts as a "pure consumer" that simply subscribes to these features and uses them to drive its visuals.

**Your animation should never need to process raw audio data.**

---

## 1. File Setup

1.  **Create Your Files:** In `src/animations/`, create your header and source files following the project's `snake_case` convention:
    *   `my_animation.h`
    *   `my_animation.cpp`

2.  **Define Your Class:** In your header, define your animation class, inheriting from `when::animations::Animation`.

    ```cpp
    // src/animations/my_animation.h
    #pragma once

    #include "animation.h"

    namespace when::animations {

    class MyAnimation : public Animation {
    public:
        // ... Constructor, Destructor ...

        // --- Core Animation Interface ---
        void init(notcurses* nc, const AppConfig& config) override;
        void update(float delta_time, const AudioMetrics& metrics, const AudioFeatures& features) override;
        void render(notcurses* nc) override;
        void bind_events(const AnimationConfig& config, events::EventBus& bus) override;
        // ... activate(), deactivate(), get_z_index(), etc. ...
    };

    }
    ```

3.  **Register Your Animation:** Open `src/animations/animation_manager.cpp`. In `load_animations`, add your new class to the `if/else if` chain. This allows the manager to construct your animation when it sees its type name in the config.

    ```cpp
    // In AnimationManager::load_animations
    } else if (cleaned_type == "MyAnimation") { // Add this line
        new_animation = std::make_unique<MyAnimation>();
    }
    ```

4.  **Add to Build System:** Open `CMakeLists.txt` and add your new `.cpp` file to the `WHEN_SOURCES` list.

---

## 2. The `AudioFeatures` API Contract

Your animation does not get raw FFT data. Instead, it receives the clean, easy-to-use `AudioFeatures` struct on every frame. This is your primary toolkit for creating visuals.

```cpp
// From: src/audio/audio_features.h
struct AudioFeatures {
    // Smoothed energy bands
    float bass_energy;      // Low frequencies after perceptual weighting + smoothing
    float mid_energy;       // Mid frequencies after perceptual weighting + smoothing
    float treble_energy;    // High frequencies after perceptual weighting + smoothing
    float total_energy;     // Aggregate energy across all bands

    // Explicit band envelopes (attack/release controlled in when.toml)
    float bass_envelope;
    float mid_envelope;
    float treble_envelope;

    // Instantaneous (pre-smoothed) energies for fast-reacting visuals
    float bass_energy_instantaneous;
    float mid_energy_instantaneous;
    float treble_energy_instantaneous;
    float total_energy_instantaneous;

    // Rhythmic tracking
    bool  beat_detected;
    float beat_strength;
    bool  bass_beat;
    bool  mid_beat;
    bool  treble_beat;
    float bpm;
    float beat_phase;
    float bar_phase;
    bool  downbeat;

    // Spectral shape
    float spectral_centroid;
    float spectral_flatness;    // Higher = noisier/whiter, lower = tonal

    // Harmonic content (optional)
    std::array<float, 12> chroma; // Normalised pitch-class intensities C..B
    bool chroma_available;        // False when chroma analysis is disabled/offline

    // Raw DSP context
    std::span<const float> band_flux; // Per-band onset deltas
};
```

### Choosing the right field for your animation

- Use the **smoothed energies or envelopes** when you want graceful, musical motion. Tweak the `dsp.smoothing_attack` and `dsp.smoothing_release` options in `when.toml` to globally adjust how responsive these values feel.
- Use the **instantaneous energies** for sharp, percussive reactions—e.g., screen shakes or particle bursts tied to fast transients.
- The **beat flags** (`beat_detected`, `bass_beat`, etc.) let you trigger one-off events. Combine them with `beat_phase`/`bar_phase` when you need metronomic placement.
- **Spectral flatness** shines when differentiating noise-like textures (hi-hats, crowd noise) from tonal content (pads, vocals). Higher values mean "noisier." Consider mapping it to texture, particle variance, or color saturation.
- The **chroma vector** gives you musical pitch information. Only consume it when `chroma_available` is true; this respects the new configuration switches so you don't accidentally rely on disabled data.

## 3. Implementing Your Animation

### `init()`
This is where you perform one-time setup.
- Create your `ncplane` and store it as a member variable.
- Load any custom parameters from the `config` object.

### `bind_events()`
This is where you connect your animation to the core engine. For most use cases, you should use the standard helper function which ensures your `update()` method is called on every frame.

```cpp
// In my_animation.cpp
#include "animation_event_utils.h" // Include the helper

void MyAnimation::bind_events(const AnimationConfig& config, events::EventBus& bus) {
    // This automatically handles activation and calls update() on every frame
    // with the latest timing, metrics, and audio features.
    bind_standard_frame_updates(this, config, bus);
}
```
The helper also supports conditional activation based on triggers in your `when.toml` file (see `animation_event_utils.h` for details).

### `update()`
This is the heart of your animation's logic. Use the `features` struct to change your animation's state.

```cpp
// In my_animation.cpp
void MyAnimation::update(float delta_time, const AudioMetrics& metrics, const AudioFeatures& features) {
    // Example: Change scale based on bass and flash on beat
    float target_scale = 1.0f + features.bass_energy * 2.0f;
    current_scale_ += (target_scale - current_scale_) * 0.1f; // Smooth the change

    if (features.beat_detected) {
        this->flash_color_ = 0xFFFFFF;
    } else {
        this->flash_color_ = 0x000000; // Fade back
    }
}
```

### `render()`
This is your drawing function. It should only read from your animation's state variables and draw to the screen.

```cpp
// In my_animation.cpp
void MyAnimation::render(notcurses* nc) {
    if (!is_active_ || !plane_) {
        return;
    }
    ncplane_erase(plane_); // Clear the plane first

    // Draw a circle whose radius is based on the state calculated in update()
    // ... drawing logic using current_scale_ ...

    // Apply the flash color
    ncplane_set_fg_rgb8(plane_, ...);
}
```

---

## 4. Configuration (`when.toml`)

Make your animation customizable by adding parameters to `when.toml` and the C++ config structs.

1.  **Add to `config.h`:** Open `src/config.h` and add your new parameter to the `AnimationConfig` struct. Give it a sensible default.

    ```cpp
    // In struct AnimationConfig
    float my_animation_speed = 10.0f;
    ```

2.  **Add to `config_loader.cpp` (or similar):** Update the config loading logic to parse your new parameter from the `toml::table`.

3.  **Use in Your Code:** In your animation's `init` or `load_parameters_from_config` method, read the value from the config object.

### Audio analysis controls you can rely on

The audio subsystem now exposes a few knobs that directly influence the richness of the `AudioFeatures` you receive:

| Option | Location | What it does |
| --- | --- | --- |
| `dsp.smoothing_attack` / `dsp.smoothing_release` | `[dsp]` table | Controls the global attack/release time of the band envelopes. Lower values follow the waveform more tightly; higher values produce silkier motion. |
| `dsp.enable_spectral_flatness` | `[dsp]` table | Enables the tonal-vs-noise analyzer. Disable it to save CPU if your animation does not need `spectral_flatness`. |
| `dsp.enable_chroma` | `[dsp]` table | Enables 12-bin chroma pitch tracking. Leave it off for lightweight setups or when no animation consumes harmonic data. |

Your animation should read the `AudioFeatures` defensively: always check `chroma_available` before using the chroma array, and expect `spectral_flatness` to be zero when the analysis is disabled. This keeps visuals robust across different installations.

---

## 5. Golden Rules for Clean Development

*   **DO NOT** attempt to process raw audio. Your animation is a **consumer**, not an analyzer.
*   **DO** contain all logic within your animation's class.
*   **DO** use the `AudioFeatures` struct as your single source of truth for the music's state.
*   **DO** define all custom options in `config.h` and `when.toml`. Avoid hard-coded "magic numbers".
*   **DO** clean up resources. If you create an `ncplane`, destroy it in your destructor.
*   **DO** keep `update()` (logic) separate from `render()` (drawing).
