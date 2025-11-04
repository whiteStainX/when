# Project Architecture: `when` Audio Visualizer

This document outlines the high-level architecture of the `when` audio visualizer. The design is centered around a decoupled, event-driven model that promotes modularity and extensibility.

## Core Modules and Their Responsibilities

- **`main.cpp`**: The application entry point. It handles command-line parsing, initializes all core modules, and runs the main application loop.
- **`AudioEngine`**: Responsible for all audio input. It captures audio from a microphone or system, or streams from a file, providing raw audio samples.
- **`DspEngine`**: Performs digital signal processing. Its primary functions are Fast Fourier Transform (FFT), beat detection, and spectral analysis. **It acts as a primary event emitter**, publishing events like `BeatDetectedEvent` and `SpectralNoveltyEvent`.
- **`EventBus`**: The central dispatcher for the event-driven system. It receives events from publishers and forwards them to subscribers, decoupling the components.
- **`AnimationManager`**: Orchestrates the animation subsystem. It loads animations from the configuration, publishes the main `FrameUpdateEvent` to the `EventBus` on each frame, and manages the rendering of all active animations.
- **`Animation` (Interface)**: The base class for all visual effects. Concrete animations inherit from this, implement their visual logic, and subscribe to events via the `bind_events` method.
- **`Config`**: Manages application configuration, loading settings from `when.toml`.
- **`PluginManager`**: Provides an extensible plugin system, allowing custom code to react to application events.

## Data Flow and Interactions (Event-Driven)

```
+-------------+   reads   +---------------+   pushes   +-------------------+
|  main.cpp   |---------->|  AudioEngine  |----------->|     DspEngine     |
| (Main Loop) |           +---------------+            |  (Event Emitter)  |
+-------------+                                        +---------+---------+
      |                                                            |
      | 1. On each frame, calls...                                 | 2. Publishes specific
      |                                                            |    audio events (Beat,
      v                                                            |    Novelty, etc.)
+------------------+  3. Publishes general    +-------------+      v
| AnimationManager |----------------------->|             |--------------------->+--------------------+
| (Event Emitter)  |   FrameUpdateEvent      |  EventBus   | 4. Dispatches events | Animation Instances|
+------------------+                         |             |--------------------->| (Subscribers)      |
                                             +-------------+                      +--------------------+
```

### Explanation of Data Flow:

1.  **Audio Processing:** In the main loop, `main.cpp` reads raw audio from the `AudioEngine` and pushes it to the `DspEngine`.
2.  **DSP Event Publishing:** The `DspEngine` analyzes the audio and publishes specific, high-level events (e.g., `BeatDetectedEvent`, `SpectralNoveltyEvent`) directly to the `EventBus`.
3.  **Frame Event Publishing:** `main.cpp` calls `animation_manager.update_all()`, which publishes the general `FrameUpdateEvent` containing timing information and raw FFT data.
4.  **Event Dispatching & Animation Logic:** The `EventBus` forwards all events to the `Animation` instances that have subscribed to them. The animations react to these events to update their internal state and visuals.
5.  **Rendering:** After events are published, `main.cpp` calls `animation_manager.render_all()`, which renders all active animations.

## Module APIs (High-Level)

### `DspEngine`

- **Constructor**: `DspEngine(sample_rate, channels, fft_size, hop_size, bands, event_bus)`
- `push_samples(samples, count)`: Feeds raw audio for processing and triggers event publishing.

### `AnimationManager`

- `load_animations(nc, app_config)`: Loads all animations from the config and calls their `bind_events` method.
- `update_all(delta_time, metrics, bands, beat_strength)`: Publishes the frame's general `FrameUpdateEvent`.
- `render_all(nc)`: Renders all active animations.

### `Animation` (Interface)

- `virtual void init(nc, config)`: For one-time setup.
- `virtual void bind_events(config, bus)`: **Crucial method** where the animation subscribes to events from the `EventBus`.
- `virtual void update(...)`: Logic to run on a `FrameUpdateEvent` (if subscribed).
- `virtual void render(nc)`: Draws the animation's current state.

### `EventBus`

- `subscribe<T>(handler)`: Allows a listener to subscribe to an event of type `T`.
- `publish<T>(event)`: Publishes an event to all registered subscribers.

## Design Principles

- **Decoupling:** Components are independent. The `DspEngine` does not know about animations, and animations do not know where audio events originate.
- **Modularity:** All logic for an animation is contained within its class.
- **Extensibility:** New animations can be created and integrated without modifying core systems.
