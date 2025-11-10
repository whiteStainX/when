# Audio Engine Upgrade: Implementation Plan

This document provides a detailed, phased implementation plan for the audio engine upgrade outlined in `AUDIO_UPGRADE.md`. Each phase is designed to deliver a significant enhancement to the `AudioFeatures` API, with steps that are verifiable and build upon one another.

## Phase 0: Architectural Groundwork

**Goal:** Prepare the DSP/feature extraction boundary so the richer feature set can be implemented without violating the project’s real-time constraints.

### Step 0.1: Make `FeatureExtractor` Stateful

-   Drop the `const` qualifier from `FeatureExtractor::process(...)` and add persistent members for weighting curves, envelopes, onset history, tempo trackers, and any other buffers called out in later phases.
-   Ensure these buffers are preallocated in the constructor (or via `prepare(...)`) to keep allocations out of the audio callback. Provide a `reset()` hook for sample-rate or configuration changes.
-   Update all call sites (notably `DspEngine`) to use the new mutable API.

### Step 0.2: Expand the Input Data Contract

-   In `DspEngine`, retain the per-bin FFT magnitudes (and phase if needed) before log-band aggregation. Bundle them with the existing log-band energies when invoking `FeatureExtractor::process`.
-   Define a lightweight struct (e.g., `FeatureInputFrame`) that carries: raw FFT magnitudes, optional phase, unsmoothed band energies, the smoothed energies (if still computed in DSP), and the per-band spectral flux values that the DSP loop already knows how to calculate.
-   Thread this struct through the call stack so later phases can rely on the richer context without further signature churn.

### Step 0.3: Decide Where Energy Smoothing Lives

-   Choose whether the envelope follower (attack/release smoothing) belongs in `DspEngine` or `FeatureExtractor`.
-   If the decision is to move smoothing into `FeatureExtractor`, expose both the instantaneous and smoothed energies to downstream consumers so visuals can select the most appropriate value. Remove the redundant smoothing block from `DspEngine` once parity is confirmed.
-   Document the decision in `DESIGN.md` to keep future contributions consistent.

### Step 0.4: Expose Per-Band Flux

-   Modify `DspEngine` so the per-band spectral flux deltas it already computes are stored alongside the energies in the `FeatureInputFrame`.
-   Validate that these deltas update correctly across sample-rate and FFT-size changes, and add regression checks if possible (unit tests or debug assertions).
-   Remove the legacy path that recalculates flux in `FeatureExtractor` once the shared data is plumbed through.

## Phase 1: Perceptual Energy & Envelopes

**Goal:** Refine all energy-based features to be more musically and perceptually accurate. This will make existing animations feel more natural and intuitive.

### Step 1.1: Implement A-Weighting Filter

-   **Why It Matters (Music Theory):** The human ear is not a linear microphone. We are far more sensitive to frequencies in the 2-5 kHz range than to very low bass or very high treble at the same volume. A-weighting is a standard curve that mimics this human hearing response. By applying it to our FFT data, our `bass_energy` and `treble_energy` will more accurately reflect perceived "punch" and "sizzle" rather than just raw physical energy.

-   **How to Implement:**
    1.  In `FeatureExtractor`, maintain a precomputed `a_weighting_curve_` sized to the FFT bin count (initialised in the constructor or `prepare(...)`).
    2.  When processing a frame, multiply the raw FFT magnitudes supplied via `FeatureInputFrame` by the weighting curve before any band aggregation.
    3.  Aggregate the weighted bins into bass/mid/treble energies (reuse existing band mappings) so the perceptual bias is baked in before envelopes or normalization are applied.
    4.  Cache the weighted magnitudes if later Tier 3 features (flatness, chroma) need them without recomputing.
    5.  Expose a configuration toggle (e.g., `dsp.apply_a_weighting`) so installations that favour raw spectra can disable the perceptual curve without recompiling.

### Step 1.2: Implement Attack/Release Envelopes

-   **Why It Matters (Music Production):** Audio compressors use separate "attack" and "release" times to control dynamics. A fast attack allows the compressor to react instantly to a loud sound, while a slower release makes the return to normal volume smooth and prevents "pumping." We can use the same principle for our visuals.

-   **How to Implement:**
    1.  In `FeatureExtractor`, for each energy feature (e.g., `bass_energy`), add a corresponding envelope member (e.g., `bass_envelope_`).
    2.  In `process()`, after calculating the raw `bass_energy` for the current frame (using weighted bins), apply the following logic if `FeatureExtractor` now owns smoothing:
        ```cpp
        float target = features.bass_energy;
        float current = this->bass_envelope_;
        float smoothing_factor;

        if (target > current) {
            // The sound is getting louder, react quickly.
            smoothing_factor = config_.attack_smoothing; // e.g., a high value like 0.8
        } else {
            // The sound is getting quieter, fade out smoothly.
            smoothing_factor = config_.release_smoothing; // e.g., a low value like 0.1
        }

        this->bass_envelope_ = current + (target - current) * smoothing_factor;
        features.bass_envelope = this->bass_envelope_;
        ```
    3.  Add `attack_smoothing` and `release_smoothing` parameters to the `Config` struct to make them tunable. If smoothing remains in `DspEngine`, expose the DSP-owned envelope via the `FeatureInputFrame` instead and skip the duplicate logic here.

## Phase 2: Advanced Rhythm Analysis

**Goal:** Go beyond simple beat detection to provide a stable tempo and phase clock, enabling animations to lock perfectly to the musical grid.

### Step 2.1: Implement Multi-Band Onset Detection

-   **Why It Matters (Music Theory):** A song's rhythm is carried by different instruments in different frequency ranges. The kick drum provides the bass pulse, the snare provides the backbeat in the mids, and hi-hats provide fast subdivisions in the treble. Detecting onsets in these bands separately allows for much more detailed rhythmic response.

-   **How to Implement:**
    1.  Consume the per-band flux values delivered in `FeatureInputFrame`. If the DSP side only sends deltas, accumulate them into short history buffers inside `FeatureExtractor` for adaptive thresholds.
    2.  Maintain separate flux averages and thresholds for each band.
    3.  Set the `features.bass_beat`, `features.mid_beat`, and `features.treble_beat` booleans based on whether each band's flux exceeds its respective threshold.

### Step 2.2: Implement Tempo (BPM) and Phase Tracking

-   **Why It Matters (Music Theory):** Tempo (BPM) is the fundamental speed of a piece of music. A phase clock that is locked to this tempo allows animations to perform periodic movements (pulses, strobes, waves) that are perfectly in sync with the music, rather than just reacting to it.

-   **How to Implement:**
    1.  In `FeatureExtractor`, create a history buffer of recent global onset strengths (e.g., the last 2-4 seconds) derived from the shared flux metrics.
    2.  On each frame, perform an autocorrelation (or comb filter bank) on this history buffer. The peak of the correlation result corresponds to the most likely period of the beat.
    3.  Convert this period to a BPM value. Heavily smooth this value over time to get a stable `features.bpm`, and expose confidence metrics if available.
    4.  Maintain a `beat_phase_` accumulator. On each frame, increment it by `(delta_time * (features.bpm / 60.0))`.
    5.  When a strong beat is detected, "re-sync" the phase by nudging it closer to 0 or 1, while keeping the adjustment small enough to avoid visible jitter.
    6.  When `beat_phase_` exceeds 1.0, wrap it back to `beat_phase_ - 1.0` and advance a bar counter to determine the downbeat (e.g., mark every 4th beat as the bar reset for 4/4).

## Phase 3: Timbre & Harmony

**Goal:** Analyze the tonal characteristics of the audio to drive mood and color in a musically intelligent way.

### Step 3.1: Implement Spectral Flatness

-   **Why It Matters (Acoustics):** Spectral flatness is a measure of how "noisy" vs. "tonal" a sound is. A pure sine wave has very low flatness (one peak in the spectrum), while white noise has very high flatness (energy is spread evenly). A flute is tonal; a cymbal crash is noisy.

-   **How to Implement:**
    1.  The formula is the *geometric mean* of the spectrum divided by the *arithmetic mean*.
    2.  In `process()`, after calculating (or retrieving) the cached `weighted_bins`/`weighted_bands` from Phase 1:
        ```cpp
        double geometric_mean_sum = 0.0;
        double arithmetic_mean_sum = 0.0;
        for (float energy : weighted_bins) {
            geometric_mean_sum += std::log(energy);
            arithmetic_mean_sum += energy;
        }
        double geometric_mean = std::exp(geometric_mean_sum / fft_bin_count);
        double arithmetic_mean = arithmetic_mean_sum / fft_bin_count;

        features.spectral_flatness = geometric_mean / arithmetic_mean;
        ```
    3.  Guard against zeros (e.g., clamp to a tiny epsilon before `log`) to maintain numerical stability.
    4.  This feature is great for controlling the "grain" or "noise" parameter in the `PleasureAnimation`.

### Step 3.2: Implement Chroma Features (Chromagram)

-   **Why It Matters (Music Theory):** A chromagram is a 12-element vector representing the intensity of the 12 pitch classes (C, C#, D, etc.) in the music. It tells you what notes and chords are playing, ignoring the octave. This is the key to harmonic-aware visuals.

-   **How to Implement:**
    1.  This is the most complex step. In `FeatureExtractor`, create a mapping that assigns each FFT bin to one of the 12 pitch classes. This requires converting the linear frequency of each bin to a logarithmic MIDI note number.
    2.  For each frame, iterate through the raw (or perceptually weighted) FFT bins supplied via `FeatureInputFrame`. For each bin, add its energy to the corresponding pitch class in a 12-element `chroma` vector.
    3.  Normalize the resulting vector.
    4.  **Benefit**: You can now map this vector to a color wheel. For example, if the 'C' and 'G' elements are strongest, you can tint the scene with colors associated with a C-Major chord, making the visuals harmonically reactive.
    5.  **Performance**: This should be an optional feature, enabled via config, as it adds another loop over the FFT bins.
