# DESIGN.md

## 1. Goal

To create a real-time, music-responsive terminal application that visually imitates the "Unknown Pleasures" album cover. The visualization will be rendered using Braille characters in a `notcurses` TUI, with nearer lines dynamically occluding farther lines based on audio frequency data.

## 2. High-Level Architecture

The application follows a modular, pipeline-based architecture. Raw audio is captured, processed into frequency data, transformed into a visual representation, and finally rendered to the screen.

```
+------------------+      +-------------------+      +----------------------+
|                  |      |                   |      |                      |
|   AudioEngine    +----->+     DspEngine     +----->+  PleasureVisualizer  |
|  (miniaudio)     |      |     (kissfft)     |      |   (Occlusion Logic)  |
|                  |      |                   |      |                      |
+------------------+      +-------------------+      +----------------------+
       | (Raw audio)                                         | (Braille buffer)
       |                                                       |
       v                                                       v
+-----------------------------------------------------------------------------+
|                                                                             |
|                                Main App Loop                                |
|                                 (main.cpp)                                  |
|                                                                             |
+-----------------------------------------------------------------------------+
       ^                                                       |
       | (User input 'q')                                      | (Render frame)
       |                                                       v
+------------------+      +-------------------+      +----------------------+
|                  |      |                   |      |                      |
|    Notcurses     +<-----+   ConfigLoader    |<-----+       when.toml      |
|    (Display)     |      |    (toml++)       |      |      (Config File)   |
|                  |      |                   |      |                      |
+------------------+      +-------------------+      +----------------------+
```

## 3. Folder Setup

The project will be organized to maintain a clear separation of concerns.

```
when/
├── build/                  // Build directory
├── external/               // 3rd-party libraries
├── src/
│   ├── audio_engine.h      // Audio capture interface
│   ├── audio_engine.cpp    // Audio capture implementation
│   ├── dsp.h               // Digital Signal Processing interface (FFT)
│   ├── dsp.cpp             // DSP implementation
│   ├── ConfigLoader.h      // Configuration loading interface
│   ├── ConfigLoader.cpp    // Configuration loading implementation
│   ├── visualizer.h        // Abstract base class for visualizations
│   ├── pleasure.h          // "Pleasure" visualization class header
│   ├── pleasure.cpp        // "Pleasure" visualization class implementation
│   └── main.cpp            // Main application entry point and loop
├── CMakeLists.txt          // Build script
├── when.toml               // Configuration file
├── PLAN.md                 // Development plan
└── DESIGN.md               // This design document
```

## 4. Components

Each component has a single, well-defined responsibility.

-   **Main (`main.cpp`):** The central orchestrator. It initializes all components, runs the main application loop, pumps data between components, and handles user input for termination.

-   **AudioEngine (`audio_engine.h/.cpp`):** Responsible for capturing raw audio samples using `miniaudio`. It can capture from a microphone or, if specified, from the system's audio output ("black hole"). It makes the raw sample data available via a ring buffer.

-   **DspEngine (`dsp.h/.cpp`):** Consumes raw audio samples from the `AudioEngine`. It uses the `kissfft` library to perform a Fast Fourier Transform (FFT), converting the time-domain audio signal into frequency-domain data (i.e., frequency magnitudes).

-   **ConfigLoader (`ConfigLoader.h/.cpp`):** Parses the `when.toml` file using `toml++`. It provides type-safe configuration data to other components, allowing for easy tuning of visualization parameters without recompiling.

-   **Visualizer (`visualizer.h`):** An abstract base class that defines the common interface for any visualization effect. This ensures that new visualizations can be added in a modular way. It will define a primary method, e.g., `render(const DspData&, ncplane*)`.

-   **PleasureVisualizer (`pleasure.h/.cpp`):** A concrete implementation of the `Visualizer` interface. This is the core of the project, containing the logic to:
    1.  Map frequency data to the vertical displacement of the lines.
    2.  Implement the "skyline" algorithm for line occlusion.
    3.  Generate a 2D buffer of Braille dots representing the final scene.
    4.  Convert the dot buffer into Unicode Braille characters for rendering.

-   **Notcurses (Library):** The rendering backend. It is managed by `main.cpp` and is responsible for all terminal drawing operations.
