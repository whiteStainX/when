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
    // Energy Bands
    float bass_energy;      // Energy in the low-frequency range
    float mid_energy;       // Energy in the mid-frequency range
    float treble_energy;    // Energy in the high-frequency range
    float total_energy;     // Overall energy across all bands

    // Rhythmic Features
    bool  beat_detected;    // True for a single frame when a beat occurs
    float beat_strength;    // The confidence/strength of the detected beat

    // Spectral Features
    float spectral_centroid; // The "brightness" of the sound (0.0=dark, 1.0=bright)
};
```

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

---

## 5. Golden Rules for Clean Development

*   **DO NOT** attempt to process raw audio. Your animation is a **consumer**, not an analyzer.
*   **DO** contain all logic within your animation's class.
*   **DO** use the `AudioFeatures` struct as your single source of truth for the music's state.
*   **DO** define all custom options in `config.h` and `when.toml`. Avoid hard-coded "magic numbers".
*   **DO** clean up resources. If you create an `ncplane`, destroy it in your destructor.
*   **DO** keep `update()` (logic) separate from `render()` (drawing).
