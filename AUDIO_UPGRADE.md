# Audio Engine Upgrade Plan

This document outlines the design for a significant upgrade to the `FeatureExtractor` service. The goal is to produce a comprehensive, professional-grade suite of audio features that will provide rich, nuanced data for driving sophisticated visuals, starting with the `PleasureAnimation`.

This plan is designed with performance in mind, categorizing features into tiers based on their computational cost and artistic impact.

## 1. The Expanded `AudioFeatures` API Contract

The `AudioFeatures` struct will be expanded to become a much richer data contract. It will be organized by feature type and will include both raw, frame-by-frame values and smoothed, "envelope" versions for more stable animations.

```cpp
struct AudioFeatures {
    // --- Tier 1: Core Energy & Rhythm (Low CPU Cost) ---

    // Raw, frame-by-frame energy values
    float bass_energy;
    float mid_energy;
    float treble_energy;
    float total_energy;

    // Smoothed, slow-moving energy envelopes
    float bass_envelope;
    float mid_envelope;
    float treble_envelope;
    float total_envelope;

    // Core beat detection
    bool  beat_detected;    // True for one frame on beat onset
    float beat_strength;    // Strength of the current beat

    // --- Tier 2: Advanced Rhythm & Spectral (Moderate CPU Cost) ---

    // Multi-band beat detection
    bool  bass_beat;        // Beat detected primarily in the bass band (kick drum)
    bool  mid_beat;         // Beat detected in the mid band (snare)
    bool  treble_beat;      // Beat detected in the treble band (hi-hat)

    // Spectral characteristics
    float spectral_centroid;        // "Brightness" of the sound
    float spectral_centroid_norm;   // Centroid normalized by total energy
    float spectral_flux;            // "Novelty", how much the spectrum is changing

    // --- Tier 3: Advanced Tonal Analysis (High CPU Cost, Optional) ---

    // Pitch and harmony detection
    bool  pitch_detected;           // Is there a clear, discernible musical note?
    float pitch_frequency;          // The fundamental frequency (in Hz) of the note
    // Example: Could be expanded to include note name (e.g., "C#4")
};
```

## 2. Feature Implementation Strategy

The features will be implemented within the `FeatureExtractor` class, with a focus on efficiency.

### Tier 1: Core Features (Highest Priority)

This tier refines what we already have and is essential for the `PleasureAnimation`.

1.  **Energy Bands (`bass_energy`, etc.)**:
    -   **Implementation**: The current implementation is already efficient. We will continue to use it.

2.  **Energy Envelopes (`bass_envelope`, etc.)**:
    -   **Implementation**: For each raw energy value, we will add a corresponding envelope value. This will be calculated using a simple exponential smoothing filter (the same technique already used for `global_magnitude_`).
    -   **Performance**: Very low cost, just a few multiplications and additions per feature.
    -   **Benefit**: Provides a "slow" version of each energy metric for free, allowing animations to react to both immediate and averaged energy without implementing their own smoothing.

3.  **Core Beat Detection**:
    -   **Implementation**: The current spectral flux-based algorithm is a good baseline. We will keep it as the primary `beat_strength` source.

### Tier 2: Advanced Features (Next Priority)

These features add significant artistic potential.

1.  **Multi-Band Beat Detection (`bass_beat`, etc.)**:
    -   **Implementation**: The beat detection algorithm (calculating spectral flux) will be run independently on the FFT data for the bass, mid, and treble frequency ranges. This will produce three separate flux values, and thus three separate beat triggers.
    -   **Performance**: Moderate cost. It triples the beat detection workload, but this is still a small part of the overall DSP chain compared to the FFT itself.
    -   **Benefit**: This is a huge upgrade. It allows visuals to react differently to kick drums, snares, and hi-hats, which is key to creating detailed, rhythmically accurate animations.

2.  **Spectral Flux**:
    -   **Implementation**: This is the raw value of the change in the spectrum from the previous frame. The current beat detector already calculates this, so we just need to expose a normalized version of it.
    -   **Performance**: Almost free, as the value is already computed.
    -   **Benefit**: A great trigger for "novelty" effects, such as when a new instrument enters the track.

3.  **Normalized Spectral Centroid**:
    -   **Implementation**: The current centroid calculation is good. The normalized version is simply `spectral_centroid / total_energy`.
    -   **Performance**: Very low cost.
    -   **Benefit**: A centroid value that is less affected by overall volume, making it a more stable parameter for controlling things like color.

### Tier 3: Tonal Features (Optional / Future Goal)

This is the most computationally expensive and complex tier.

1.  **Pitch Detection**:
    -   **Implementation**: This requires a dedicated pitch detection algorithm (e.g., YIN, AMDF, or a machine learning model). This is a significant undertaking. It would involve analyzing the waveform to find its fundamental frequency.
    -   **Performance**: **High cost**. A robust pitch detection algorithm can be as expensive as the FFT itself. This should be an optional feature that can be disabled in the configuration.
    -   **Benefit**: The "holy grail" for synesthetic visuals. Allows for direct mapping of musical notes to colors or shapes, creating visuals that are harmonically in sync with the audio.

## 3. Configuration and Performance

-   **Configurability**: The `FeatureExtractor`'s configuration will be expanded. Each feature, especially the expensive ones (like Pitch Detection), will be individually **enabled or disabled** via the `when.toml` file.
-   **Performance First**: The implementation will prioritize performance. All calculations will use `float` where possible, and algorithms will be chosen for their efficiency on a CPU. We will avoid any memory allocations (like creating new vectors) within the real-time processing loop.

This plan provides a clear roadmap for evolving the audio engine into a truly professional-grade service, enabling a new level of sophistication for the `PleasureAnimation` and all future visualizers.
