# Audio Engine Upgrade Plan

This document outlines the design for a significant upgrade to the `FeatureExtractor` service. The goal is to produce a comprehensive, professional-grade suite of audio features that will provide rich, nuanced data for driving sophisticated visuals, like the `PleasureAnimation`.

## Current Status Recap (2025-11-08)

- `FeatureExtractor` is presently stateless, with a `const process(...)` API. It only derives a handful of coarse metrics (three band averages, total energy, beat flag, spectral centroid) from log-spaced magnitudes that arrive already smoothed.
- `DspEngine` computes the FFT, collapses magnitudes into log bands, applies attack/release smoothing, and calculates an aggregated spectral flux figure before handing control to the extractor. Phase information and per-bin magnitudes are discarded at this boundary.
- The existing beat detection already keeps per-band flux deltas internally, but only the summed beat strength is exposed downstream.

The upgrade described below therefore requires structural changes before the new features can be layered in. The plan explicitly calls out these prerequisites so the implementation can move forward without compromising the engine's real-time guarantees.

This plan is designed with performance in mind, categorizing features into tiers based on their computational cost and artistic impact.

## 1. Guiding Principles

- **Perceptual Accuracy**: Features should reflect what a human _hears_, not just raw signal power. This involves perceptual weighting (A-weighting), loudness normalization (LUFS), and psychoacoustically-aware algorithms.
- **Musical Intelligence**: The engine should understand not just _that_ a sound occurred, but its musical role—is it a beat, a melody, a texture? This means moving from onset detection to tempo tracking, beat grids, and harmonic analysis.
- **Performance by Design**: The system must operate in real-time. This requires efficient algorithms, optional features, performance monitoring, and a disciplined approach to memory and processing. Expensive features should be optional or run at a lower rate.

## 2. The Expanded `AudioFeatures` API Contract

The `AudioFeatures` struct will be expanded to become a much richer data contract, organized by musical characteristic.

```cpp
struct AudioFeatures {
    // --- Tier 1: Core Energy & Loudness ---
    float short_term_loudness;  // Perceptually-weighted loudness (LUFS-like)
    float bass_energy;          // A-weighted bass energy
    float mid_energy;           // A-weighted mid energy
    float treble_energy;        // A-weighted treble energy
    float bass_envelope;        // Smoothed bass envelope (fast attack, slow release)
    float mid_envelope;         // Smoothed mid envelope
    float treble_envelope;      // Smoothed treble envelope

    // --- Tier 2: Rhythm & Structure ---
    bool  beat_detected;        // A significant beat was detected this frame
    float beat_strength;        // Strength of the current beat
    float bpm;                  // Estimated tempo in beats per minute
    float beat_phase;           // Phase accumulator (0-1) locked to the beat
    float bar_phase;            // Phase accumulator (0-1) for a 4/4 measure
    bool  downbeat;             // True if the current beat is the first in a measure

    // --- Tier 3: Timbre & Harmony (Moderate CPU) ---
    float spectral_centroid;    // "Brightness" of the sound
    float spectral_flatness;    // Tonal vs. noisy characteristic
    float spectral_rolloff;     // "Brightness tilt" of the spectrum
    // std::vector<float> chroma; // 12-bin vector representing musical pitch classes (C, C#, D...)
    // std::vector<float> mfcc;   // 4-8 coefficients for a compact timbre signature

    // --- Tier 4: Source Separation (High CPU, Optional) ---
    // Separate features for the percussive and harmonic elements of the sound
    // float percussive_energy;
    // float harmonic_energy;
};
```

## 3. Feature Implementation Strategy

Before tackling the tiers below, complete the architectural adjustments in Phase 0 of `AUDIO_PLAN.md`:

- Make `FeatureExtractor` stateful (drop the `const` qualifier on `process`, add persistent buffers for envelopes, onset history, tempo tracking, etc.).
- Retain and pass per-bin FFT magnitudes (and phase where necessary) from `DspEngine` so higher-level perceptual and harmonic metrics have access to the raw spectrum.
- Decide where energy smoothing lives to avoid double-filtering (either forward unsmoothed data to the extractor or centralize the smoothing logic in one place).
- Expose the per-band spectral flux that `DspEngine` already calculates so multi-band onset logic can reuse it rather than re-deriving the same values.

### Tier 1: Perceptual Loudness & Energy

1.  **A-Weighting Pre-Emphasis**:

    - **What**: Before calculating energy, apply an A-weighting filter curve to the FFT bins. This boosts frequencies where the human ear is most sensitive and attenuates those where it is not.
    - **Benefit**: `bass_energy` will now represent perceived "punch" and `treble_energy` will represent perceived "sizzle," making them map more intuitively to visuals.

2.  **Short-Term Loudness (LUFS-like)**:

    - **What**: Implement a real-time approximation of the BS.1770 loudness standard. This involves a specific pre-filtering (K-weighting) and averaging over a ~400ms window.
    - **Benefit**: A master gain control for all visuals. An orchestral piece and a heavily compressed electronic track will both register at similar loudness levels, preventing visuals from blowing out or disappearing.

3.  **Attack/Release Envelopes**:
    - **What**: For each energy band (`bass`, `mid`, `treble`), create an envelope follower with separate, configurable attack and release times. Only one module (either `DspEngine` or `FeatureExtractor`) should apply the smoothing to keep transient response crisp.
    - **Benefit**: Allows for visuals that can rise instantly with a sound (`fast attack`) but fade out slowly and smoothly (`slow release`), mimicking audio compressors for a more natural feel without overdamping.

### Tier 2: Rhythm You Can Lock To

1.  **Multi-Band Onset Detection**:

    - **What**: Run the spectral flux onset detection algorithm independently on the bass, mid, and treble bands, reusing the per-band flux already computed during the DSP step whenever possible.
    - **Benefit**: The ability to distinguish between a kick drum, a snare, and a hi-hat, enabling far more detailed rhythmic animation without redundant CPU work.

2.  **Tempo (BPM) and Phase Tracking**:

    - **What**: Use autocorrelation on the onset detection curve to find the most likely tempo (BPM). Maintain a "phase accumulator" that resets on each beat and increments from 0 to 1 until the next.
    - **Benefit**: This is a massive leap. Animations can now be driven by a stable clock locked to the music's tempo. This enables perfectly synchronized periodic motions, pulses, and strobes.

3.  **Beat Grid & Downbeat Detection**:
    - **What**: Using the tracked tempo, infer a 4/4 measure. Track the current beat within the bar (1, 2, 3, 4) and provide a `bar_phase` (0 to 1 over the whole measure). Mark the first beat as the `downbeat`.
    - **Benefit**: Allows for "phrasing" in visuals. An animation can have a major accent on the downbeat and smaller accents on the other beats, making it feel structurally aware of the music.

### Tier 3: Timbre & Harmony (The Efficient Path)

1.  **Chroma Features**:

    - **What**: Implement a Chromagram. This involves mapping FFT bins to 12 "pitch classes" (C, C#, D, etc.). This is far cheaper than direct pitch detection.
    - **Benefit**: Provides the harmonic "flavor" of the music. You can use this to tint the entire scene's color palette based on the current key or chord, creating powerful mood changes.

2.  **Spectral Flatness & Zero-Crossing Rate (ZCR)**:
    - **What**: Flatness measures how noisy vs. tonal a sound is. ZCR is a very cheap proxy for "brightness" calculated on the raw audio samples.
    - **Benefit**: Excellent for controlling texture. A noisy sound (high flatness) can map to more "grain" or particle effects in the `PleasureAnimation`, while a tonal sound can map to smoother lines.

### Tier 4: Source Separation (Advanced)

1.  **Harmonic-Percussive Source Separation (HPSS)**:
    - **What**: Use median filtering across the time and frequency axes of the spectrogram to separate it into two layers: one containing horizontal features (harmonics, pads, vocals) and one with vertical features (transients, drums).
    - **Benefit**: The ultimate feature for the `PleasureAnimation`. The harmonic layer can drive the slow, undulating shapes of the lines, while the percussive layer can trigger sharp, crackling bursts of noise or light on top of them, perfectly disentangling the "flow" from the "crackle."

## 4. Real-time Performance & System Discipline

This section, inspired by your feedback, will be added to the main `DESIGN.md` to enforce best practices.

- **FFT Settings**: Standardize on 48kHz sample rate, FFT size of 2048, and a hop size of 512 (4x overlap). This provides a good balance of time and frequency resolution.
- **Performance Tiers**: All features from Tier 2 and above will be individually optional in `when.toml`. The `FeatureExtractor` will only compute what is explicitly enabled.
- **Feature Decimation**: Expensive features (Chroma, HPSS) can be configured to run at half or quarter the visual frame rate, with their results being interpolated (held or linearly faded) for the frames in between.
- **Real-time Safety**: No memory allocations (no `new`, `malloc`, `std::vector::push_back`) are allowed within the real-time audio callback or the main DSP processing loop. All buffers must be pre-allocated.
- **Input Plumbing**: The architecture should support capturing from two devices simultaneously (e.g., microphone and system loopback) and provide a mechanism (e.g., envelope subtraction) to prevent "double hits" from speaker bleed.
- **Input Conditioning**: The microphone input path should have optional high-pass and 50/60Hz notch filters to remove DC offset and electrical hum.
