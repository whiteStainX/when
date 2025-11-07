# Project Upgrade: The Audio Feature Service

This document outlines a planned architectural upgrade for the `when` audio visualizer. The goal is to refactor the existing pipeline into a more robust, decoupled, and extensible service-oriented architecture with clearly defined responsibilities and data contracts between the DSP layer and the animation runtime.

## 1. Motivation

The current architecture, while functional, creates a tight coupling between the `DspEngine` and the `Animation` instances. Animations currently receive raw FFT data and are responsible for their own feature extraction (e.g., calculating average magnitude). This has several drawbacks:

- **Repetitive Code**: Every animation that needs to react to "bass" or "treble" must reimplement the logic to calculate it from raw FFT bands.
- **Poor Separation of Concerns**: Animations are forced to be concerned with the low-level details of digital signal processing, when their sole responsibility should be visual representation.
- **Limited Extensibility**: Adding new, complex audio analysis features (like pitch or spectral centroid detection) requires modifying the `update` signature for all animations.

This upgrade will address these issues by introducing a formal **Audio Feature Service**.

## 2. The New Architecture: Audio Feature Service

The core idea is to treat audio analysis as a dedicated, centralized service that publishes a rich set of high-level, musical features. Animations will become pure consumers of this service, completely decoupled from the underlying DSP logic, while still receiving timing (`delta_time`) and diagnostics (`AudioMetrics`) from the frame pipeline.

### Architectural Diagram

```
+-------------+   pushes   +-------------------+   produces   +-----------------+
| AudioEngine |----------->|     DspEngine     |------------->| FeatureExtractor|
| (Raw Audio) |            | (FFT & Beat Detection) |              | (The "Service") |
+-------------+            +-------------------+              +--------+--------+
                                                                     |
                                                                     | 1. Publishes rich
                                                                     |    audio features
                                                                     v
+--------------------+  2. Subscribes to        +-------------+
| Animation Instances|----------------------->|             |---------------------> (No change)
| (Pure Consumers)   |   Features, not data    |  EventBus   |
+--------------------+                         |             |
                                             +-------------+
```

### Key Components

1.  **`AudioFeatures` Struct (The API Contract)**
    This struct will be the single, consistent data packet published by the service. It represents the formal "API" that all animations can rely on.

```cpp
// Defined in a new header, e.g., "src/audio/audio_features.h"
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
    float spectral_centroid; // The "brightness" of the sound
    // ... other features like spectral flux, etc., can be added later
};
```

`AudioFeatures` does **not** replace `AudioMetrics`. Metrics continue to surface low-level diagnostics (frame rate, smoothing state, etc.), while `AudioFeatures` provides musical semantics. Both structs travel together in the frame update.

2.  **`FeatureExtractor` Class (The Service Implementation)**
    - A new class responsible for all high-level feature calculations.
    - It will be owned by the `DspEngine`.
    - Its primary method will be `process(const std::vector<float>& fft_bands, float beat_strength)`, which it uses to compute and return the `AudioFeatures` struct for the current frame.

3.  **`EventBus` Integration**
    - The `DspEngine` will no longer publish raw FFT data.
    - It will call the `FeatureExtractor` and then publish a single, new event type: `AudioFeaturesUpdatedEvent`, containing the `AudioFeatures` struct.
    - The existing `FrameUpdateEvent` contract will be extended to carry `AudioFeatures` alongside `delta_time` and `AudioMetrics`. The animation manager remains subscribed to frame events so animations still receive the timing data required for easing, activation heuristics, and diagnostics.

4.  **`Animation` Base Class (The Consumer)**
    - The `update` method signature in the base `Animation` class will be changed.
    - **Old**: `virtual void update(float dt, const AudioMetrics&, const std::vector<float>& bands, float beat_strength)`
    - **New**: `virtual void update(float dt, const AudioMetrics&, const AudioFeatures& features)`
    - This change forces all animations to become pure consumers of the high-level features, completely removing their access to raw FFT data. Because `FrameUpdateEvent` continues to include `delta_time` and `AudioMetrics`, derived animations retain the timing and diagnostic information they currently rely on.

## 3. File and Directory Structure Changes

To better organize the audio processing code and formalize the new service layer, the following changes will be made to the file structure.

### New Directory

A new directory will be created to house all generalized audio analysis components:

- `src/audio/`

### New Files

The following new files will be created within the `src/audio/` directory:

1.  **`src/audio/audio_features.h`**
    - **Purpose**: Defines the `AudioFeatures` struct. This acts as the clean data contract between the audio engine and all visual animations. It will be a header-only file.

2.  **`src/audio/feature_extractor.h`**
    - **Purpose**: The header file for the `FeatureExtractor` class. It will define the class interface, including its constructor and the primary `process()` method.

3.  **`src/audio/feature_extractor.cpp`**
    - **Purpose**: The implementation file for the `FeatureExtractor` class. All the logic for calculating bass, mids, treble, spectral centroid, etc., will reside here.

### Modified Files

The following existing files will be modified to integrate the new service and to keep downstream consumers aligned with the new contract:

- **`src/dsp.h` & `src/dsp.cpp`**: The `DspEngine` will be updated to own an instance of `FeatureExtractor` and to publish `AudioFeaturesUpdatedEvent` objects. Its constructor will accept an `events::EventBus&` so the engine can emit events without owning the bus.
- **`src/main.cpp`**: The bootstrapping code will construct the shared `EventBus`, pass it to `DspEngine`, and keep using the same bus for animation and renderer wiring so all systems receive the combined frame events.
- **`src/renderer.h` & `src/renderer.cpp`**: The renderer will switch from consuming raw FFT vectors to consuming `AudioFeatures` (either directly or via `FrameUpdateEvent`). Update any helper methods that previously accessed FFT bands.
- **`src/plugins.h` & `src/plugins.cpp`**: The plugin API will be updated so plugin authors receive `AudioFeatures` instead of raw bands. Provide backwards-compatibility shims if necessary.
- **`src/animations/animation.h`**: The `Animation` base class interface will be changed. The `update()` method signature will be modified to accept the `AudioFeatures` struct.
- **`src/animations/animation_manager.cpp`**: The event dispatching logic will be updated to pass the combined `FrameUpdateEvent` payload (`delta_time`, `AudioMetrics`, and `AudioFeatures`).
- **`src/animations/pleasure_animation.h` & `src/animations/pleasure_animation.cpp`**: This animation will be refactored to become a pure consumer of the new `AudioFeatures` struct, with all internal FFT processing logic removed.
- **`CMakeLists.txt`**: The new `feature_extractor.cpp` file will be added to the list of sources to be compiled.

---

## 4. Implementation Plan

This refactoring will be performed in a series of steps to ensure a smooth transition.

**Step 1: Establish the Audio Feature contract**
- Create the new header `src/audio/audio_features.h`.
- Define the `AudioFeatures` struct with initial fields (bass, mid, treble, beat, etc.).
- Add serialization or helper utilities if needed by the renderer or plugins.

**Step 2: Implement the FeatureExtractor service**
- Create `src/audio/feature_extractor.h` and `.cpp`.
- Implement the `FeatureExtractor` class.
- Write the logic to calculate `bass_energy`, `mid_energy`, `treble_energy`, and any initial spectral features from the raw FFT bands. The frequency ranges for these bands will be configurable.
- Add unit tests or offline verification utilities to validate feature quality.

**Step 3: Inject the EventBus into DspEngine**
- Update `DspEngine`'s constructor in `src/dsp.h`/`.cpp` to accept an `events::EventBus&` (or smart pointer) and store a reference for publishing events.
- Update the `src/main.cpp` bootstrapping to construct the `EventBus` first and pass it to `DspEngine`.
- Ensure ownership lives outside `DspEngine`; the engine publishes to the bus but does not manage its lifetime.

**Step 4: Publish AudioFeaturesUpdatedEvent**
- Inside `DspEngine`, instantiate `FeatureExtractor` and call it each frame with raw FFT data and beat metrics.
- Publish an `AudioFeaturesUpdatedEvent` containing the latest `AudioFeatures`.
- Extend the existing `FrameUpdateEvent` in `src/events/frame_events.h` to include an `AudioFeatures` member while retaining `delta_time` and `AudioMetrics`.
- Update `src/animations/animation_event_utils.h` and any other helpers to work with the combined payload.

**Step 5: Update consumers to the new contract**
- Adjust `Animation::update` in `src/animations/animation.h` and all derived animations (starting with `PleasureAnimation`) to accept `AudioFeatures`.
- Update `AnimationManager` so it listens for `AudioFeaturesUpdatedEvent`, enriches the existing frame state, and dispatches the new `FrameUpdateEvent` that carries timing, metrics, and features together.
- Update the renderer and plugin APIs to consume `AudioFeatures` instead of raw FFT bands.
- Ensure `src/main.cpp` wiring passes the enriched frame data to the renderer and plugins.

**Step 6: Remove legacy FFT consumption paths**
- Delete any remaining code paths that expose raw FFT vectors to animations or plugins.
- Clean up obsolete helper functions and configuration options.
- Update documentation and examples to reference `AudioFeatures`.

## 5. Benefits of the Upgrade

- **Purity & Decoupling**: Animations are now 100% focused on visualization.
- **Reusability**: The `FeatureExtractor` provides a common, reusable set of powerful audio metrics for all future animations.
- **Extensibility**: Adding a new audio feature (e.g., `pitch`) only requires updating the `FeatureExtractor` and the `AudioFeatures` struct, with no changes needed in any animation code.
- **Improved Testability**: The audio analysis and visual components can be tested in complete isolation.
