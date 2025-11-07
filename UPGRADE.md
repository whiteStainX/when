# Project Upgrade: The Audio Feature Service

This document outlines a planned architectural upgrade for the `when` audio visualizer. The goal is to refactor the existing pipeline into a more robust, decoupled, and extensible service-oriented architecture.

## 1. Motivation

The current architecture, while functional, creates a tight coupling between the `DspEngine` and the `Animation` instances. Animations currently receive raw FFT data and are responsible for their own feature extraction (e.g., calculating average magnitude). This has several drawbacks:

- **Repetitive Code**: Every animation that needs to react to "bass" or "treble" must reimplement the logic to calculate it from raw FFT bands.
- **Poor Separation of Concerns**: Animations are forced to be concerned with the low-level details of digital signal processing, when their sole responsibility should be visual representation.
- **Limited Extensibility**: Adding new, complex audio analysis features (like pitch or spectral centroid detection) requires modifying the `update` signature for all animations.

This upgrade will address these issues by introducing a formal **Audio Feature Service**.

## 2. The New Architecture: Audio Feature Service

The core idea is to treat audio analysis as a dedicated, centralized service that publishes a rich set of high-level, musical features. Animations will become pure consumers of this service, completely decoupled from the underlying DSP logic.

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
        float bass_energy;     // Energy in the low-frequency range
        float mid_energy;      // Energy in the mid-frequency range
        float treble_energy;   // Energy in the high-frequency range
        float total_energy;    // Overall energy across all bands

        // Rhythmic Features
        bool  beat_detected;   // True for a single frame when a beat occurs
        float beat_strength;   // The confidence/strength of the detected beat

        // Spectral Features
        float spectral_centroid; // The "brightness" of the sound
        // ... other features like spectral flux, etc., can be added later
    };
    ```

2.  **`FeatureExtractor` Class (The Service Implementation)**
    - A new class responsible for all high-level feature calculations.
    - It will be owned by the `DspEngine`.
    - Its primary method will be `process(const std::vector<float>& fft_bands, float beat_strength)`, which it uses to compute and return the `AudioFeatures` struct for the current frame.

3.  **`EventBus` Integration**
    - The `DspEngine` will no longer publish raw FFT data.
    - It will call the `FeatureExtractor` and then publish a single, new event type: `AudioFeaturesUpdatedEvent`, containing the `AudioFeatures` struct.

4.  **`Animation` Base Class (The Consumer)**
    - The `update` method signature in the base `Animation` class will be changed.
    - **Old**: `virtual void update(float dt, const AudioMetrics&, const std::vector<float>& bands, float beat_strength)`
    - **New**: `virtual void update(float dt, const AudioMetrics&, const AudioFeatures& features)`
    - This change forces all animations to become pure consumers of the high-level features, completely removing their access to raw FFT data.

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

The following existing files will be modified to integrate the new service:

- **`src/dsp.h` & `src/dsp.cpp`**: The `DspEngine` will be updated to own an instance of `FeatureExtractor`. Its main processing loop will be changed to call the extractor and publish the new `AudioFeaturesUpdatedEvent` instead of raw data.
- **`src/animations/animation.h`**: The `Animation` base class interface will be changed. The `update()` method signature will be modified to accept the `AudioFeatures` struct.
- **`src/animations/pleasure_animation.h` & `src/animations/pleasure_animation.cpp`**: This animation will be refactored to become a pure consumer of the new `AudioFeatures` struct, with all internal FFT processing logic removed.
- **`src/animations/animation_manager.cpp`**: The event dispatching logic will be updated to pass the new `AudioFeatures` payload.
- **`CMakeLists.txt`**: The new `feature_extractor.cpp` file will be added to the list of sources to be compiled.

---

## 4. Implementation Plan

This refactoring will be performed in a series of steps to ensure a smooth transition.

**Step 1: Create the `AudioFeatures` Contract**
- Create the new header `src/audio/audio_features.h`.
- Define the `AudioFeatures` struct with initial fields (bass, mid, treble, beat, etc.).

**Step 2: Implement the `FeatureExtractor` Service**
- Create `src/audio/feature_extractor.h` and `.cpp`.
- Implement the `FeatureExtractor` class.
- Write the logic to calculate `bass_energy`, `mid_energy`, and `treble_energy` from the raw FFT bands. The frequency ranges for these bands will be configurable.

**Step 3: Integrate the Service into `DspEngine`**
- Modify `DspEngine` to own an instance of `FeatureExtractor`.
- In the `DspEngine`'s processing loop, call the feature extractor.
- Define the new `AudioFeaturesUpdatedEvent` and modify the `DspEngine` to publish it on the `EventBus`.

**Step 4: Refactor the `Animation` Subsystem**
- Change the `update` method signature in the `Animation` base class and all derived animation classes (i.e., `PleasureAnimation`).
- Change the `EventBus` subscription in `bind_events` from the old frame update to the new `AudioFeaturesUpdatedEvent`.

**Step 5: Update `PleasureAnimation` to be a Pure Consumer**
- Remove all FFT processing logic from `PleasureAnimation::update()`.
- Modify the logic to use the incoming `features` struct. For example, `global_magnitude_` will now be driven by `features.total_energy` or a combination of the energy bands, and beat-related effects will be driven directly by `features.beat_strength`.

## 4. Benefits of the Upgrade

- **Purity & Decoupling**: Animations are now 100% focused on visualization.
- **Reusability**: The `FeatureExtractor` provides a common, reusable set of powerful audio metrics for all future animations.
- **Extensibility**: Adding a new audio feature (e.g., `pitch`) only requires updating the `FeatureExtractor` and the `AudioFeatures` struct, with no changes needed in any animation code.
- **Improved Testability**: The audio analysis and visual components can be tested in complete isolation.
