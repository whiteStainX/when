# Project Architecture: `when` Audio Visualizer

This document outlines the high-level architecture of the `when` audio visualizer. The design is centered around a decoupled, service-oriented model that promotes modularity and extensibility by separating audio analysis from visual representation.

## Core Modules and Their Responsibilities

- **`main.cpp`**: The application entry point. It handles command-line parsing, initializes all core modules, and runs the main application loop.
- **`AudioEngine`**: Responsible for all audio input. It captures audio from a microphone or system, or streams from a file, providing raw audio samples.
- **`DspEngine`**: Orchestrates all audio processing. It performs FFT and beat detection on the raw audio stream, then uses the `FeatureExtractor` to produce high-level musical features.
- **`FeatureExtractor` (The Audio Feature Service)**: A self-contained service that consumes raw DSP data (FFT bands, beat strength) and produces a rich `AudioFeatures` struct. This struct is the clean, public-facing "API" of the audio engine.
- **`EventBus`**: The central dispatcher for the event-driven system. It receives events from publishers and forwards them to subscribers, decoupling all components.
- **`AnimationManager`**: Orchestrates the animation subsystem. It receives the latest `AudioFeatures` from the `DspEngine`, bundles them with timing data into a `FrameUpdateEvent`, publishes this event to the `EventBus`, and manages the rendering of all active animations.
- **`Animation` (Interface)**: The base class for all visual effects. Concrete animations are **pure consumers** of the `AudioFeatures` struct. They subscribe to the `FrameUpdateEvent` and use its high-level data to drive their visual logic, without any knowledge of DSP.
- **`Config`**: Manages application configuration, loading settings from `when.toml`.
- **`PluginManager`**: Provides an extensible plugin system, allowing custom code to react to application events, also consuming the `AudioFeatures` struct.

## Data Flow and Interactions (Service-Oriented)

The core of the architecture is the strict separation between the **Audio Feature Service** and its **Consumers** (the animations).

```
+-------------+   pushes   +-------------------+   produces   +------------------+
| AudioEngine |----------->|     DspEngine     |------------->| FeatureExtractor |
| (Raw Audio) |            | (FFT, Beat Strength) |              | (Audio Service)  |
+-------------+            +-------------------+              +--------+---------+
      |                                                                |
      | 1. In the main loop, DspEngine                                 |
      |    is fed raw audio and...                                     | 2. ...produces a clean
      |                                                                |    AudioFeatures struct.
      v                                                                v
+------------------+  3. Bundles features &     +-------------+  4. Dispatches the rich
| AnimationManager |     time into a single    |             |     FrameUpdateEvent to
| (Event Emitter)  |-------------------------->|  EventBus   |---------------------->+--------------------+
+------------------+     FrameUpdateEvent      |             |                       | Animation Instances|
                                               +-------------+                       | (Pure Consumers)   |
                                                                                     +--------------------+
```

### Explanation of Data Flow:

1.  **Audio Processing**: In the main loop, `main.cpp` reads raw audio from the `AudioEngine` and pushes it to the `DspEngine`.
2.  **Feature Extraction**: The `DspEngine` performs FFT and beat detection. It then passes this raw data to its internal `FeatureExtractor`, which computes a high-level `AudioFeatures` struct (containing bass, mids, treble, centroid, etc.).
3.  **Frame Event Publishing**: `main.cpp` calls `animation_manager.update_all()`, passing it the latest `AudioFeatures`. The manager combines these features with timing data (`delta_time`) into a single, rich `FrameUpdateEvent` and publishes it to the `EventBus`.
4.  **Event Dispatching & Animation Logic**: The `EventBus` forwards the `FrameUpdateEvent` to all subscribed `Animation` instances. The animations react *only* to the high-level data within this event (`event.features`) to update their state and visuals.
5.  **Rendering**: After events are published, `main.cpp` calls `animation_manager.render_all()`, which renders all active animations.

## Module APIs (High-Level)

### `DspEngine`

- **Constructor**: `DspEngine(event_bus, sample_rate, ...)`
- `push_samples(samples, count)`: Feeds raw audio for processing.
- `audio_features() const`: **Returns the latest `AudioFeatures` struct.**

### `AnimationManager`

- `load_animations(nc, app_config)`: Loads all animations from the config.
- `update_all(delta_time, metrics, features)`: Publishes the frame's `FrameUpdateEvent`.
- `render_all(nc)`: Renders all active animations.

### `Animation` (Interface)

- `virtual void init(nc, config)`: For one-time setup.
- `virtual void bind_events(config, bus)`: Subscribes the animation to the `FrameUpdateEvent`.
- `virtual void update(delta_time, metrics, features)`: **The core logic method. Consumes the high-level `AudioFeatures` struct to drive visuals.**
- `virtual void render(nc)`: Draws the animation's current state.

### `EventBus`

- `subscribe<T>(handler)`: Allows a listener to subscribe to an event of type `T`.
- `publish<T>(event)`: Publishes an event to all registered subscribers.

## Design Principles

- **Service-Oriented**: The audio analysis pipeline (`DspEngine` + `FeatureExtractor`) acts as a self-contained service. Its only public product is the `AudioFeatures` struct.
- **Strict Decoupling**: Consumers (`Animation`, `PluginManager`) are completely ignorant of DSP concepts like FFTs or frequency bands. They are coded against the stable `AudioFeatures` "API".
- **Single Source of Truth**: The `FeatureExtractor` is the single source of truth for all musical analysis. This prevents logic duplication and ensures all visual components react consistently.
- **Extensibility**: A new audio feature (e.g., pitch detection) can be added by updating only the `FeatureExtractor` and the `AudioFeatures` struct, with zero impact on existing animations. A new animation can be created that instantly has access to the full suite of audio features.