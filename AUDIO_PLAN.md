# Audio Engine Upgrade: Implementation Plan

This document provides a detailed, phased implementation plan for the audio engine upgrade outlined in `AUDIO_UPGRADE.md`. Each phase is designed to deliver a significant enhancement to the `AudioFeatures` API, with steps that are verifiable and build upon one another.

## Phase 1: Perceptual Energy & Envelopes

**Goal:** Refine all energy-based features to be more musically and perceptually accurate. This will make existing animations feel more natural and intuitive.

### Step 1.1: Implement A-Weighting Filter

-   **Why It Matters (Music Theory):** The human ear is not a linear microphone. We are far more sensitive to frequencies in the 2-5 kHz range than to very low bass or very high treble at the same volume. A-weighting is a standard curve that mimics this human hearing response. By applying it to our FFT data, our `bass_energy` and `treble_energy` will more accurately reflect perceived "punch" and "sizzle" rather than just raw physical energy.

-   **How to Implement:**
    1.  In `FeatureExtractor`, create a new private member `std::vector<float> a_weighting_curve_`.
    2.  In the constructor, pre-calculate the A-weighting value for each FFT frequency bin and store it in the curve. The formula for A-weighting is standardized and can be easily found online.
    3.  In the `process()` method, before any energy calculations, create a temporary `std::vector<float> weighted_bands`.
    4.  Populate this vector by multiplying each element of the incoming `fft_bands` with the corresponding value from `a_weighting_curve_`.
    5.  All subsequent calculations in this frame (`bass_energy`, `mid_energy`, etc.) will now use `weighted_bands` instead of the raw `fft_bands`.

### Step 1.2: Implement Attack/Release Envelopes

-   **Why It Matters (Music Production):** Audio compressors use separate "attack" and "release" times to control dynamics. A fast attack allows the compressor to react instantly to a loud sound, while a slower release makes the return to normal volume smooth and prevents "pumping." We can use the same principle for our visuals.

-   **How to Implement:**
    1.  In `FeatureExtractor`, for each energy feature (e.g., `bass_energy`), add a corresponding envelope member (e.g., `bass_envelope_`).
    2.  In `process()`, after calculating the raw `bass_energy` for the current frame, apply the following logic:
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
    3.  Add `attack_smoothing` and `release_smoothing` parameters to the `Config` struct to make them tunable.

## Phase 2: Advanced Rhythm Analysis

**Goal:** Go beyond simple beat detection to provide a stable tempo and phase clock, enabling animations to lock perfectly to the musical grid.

### Step 2.1: Implement Multi-Band Onset Detection

-   **Why It Matters (Music Theory):** A song's rhythm is carried by different instruments in different frequency ranges. The kick drum provides the bass pulse, the snare provides the backbeat in the mids, and hi-hats provide fast subdivisions in the treble. Detecting onsets in these bands separately allows for much more detailed rhythmic response.

-   **How to Implement:**
    1.  The current beat detector calculates spectral flux across all bands. Refactor this logic into a helper function: `float calculate_flux(const std::vector<float>& bands, size_t start, size_t end)`.
    2.  In `process()`, call this function three times, using the resolved indices for the bass, mid, and treble bands. This will produce `bass_flux`, `mid_flux`, and `treble_flux`.
    3.  Maintain separate flux averages and thresholds for each band.
    4.  Set the `features.bass_beat`, `features.mid_beat`, and `features.treble_beat` booleans based on whether each band's flux exceeds its respective threshold.

### Step 2.2: Implement Tempo (BPM) and Phase Tracking

-   **Why It Matters (Music Theory):** Tempo (BPM) is the fundamental speed of a piece of music. A phase clock that is locked to this tempo allows animations to perform periodic movements (pulses, strobes, waves) that are perfectly in sync with the music, rather than just reacting to it.

-   **How to Implement:**
    1.  In `FeatureExtractor`, create a history buffer of recent global onset strengths (e.g., the last 2-4 seconds).
    2.  On each frame, perform an autocorrelation on this history buffer. The peak of the autocorrelation result corresponds to the most likely period of the beat.
    3.  Convert this period to a BPM value. Heavily smooth this value over time to get a stable `features.bpm`.
    4.  Maintain a `beat_phase_` accumulator. On each frame, increment it by `(delta_time * (features.bpm / 60.0))`.
    5.  When a strong beat is detected, "re-sync" the phase by nudging it closer to 0 or 1.
    6.  When `beat_phase_` exceeds 1.0, wrap it back to `beat_phase_ - 1.0` and set `features.downbeat` to true (for a simple 4/4 measure, this can be done every 4th beat).

## Phase 3: Timbre & Harmony

**Goal:** Analyze the tonal characteristics of the audio to drive mood and color in a musically intelligent way.

### Step 3.1: Implement Spectral Flatness

-   **Why It Matters (Acoustics):** Spectral flatness is a measure of how "noisy" vs. "tonal" a sound is. A pure sine wave has very low flatness (one peak in the spectrum), while white noise has very high flatness (energy is spread evenly). A flute is tonal; a cymbal crash is noisy.

-   **How to Implement:**
    1.  The formula is the *geometric mean* of the spectrum divided by the *arithmetic mean*.
    2.  In `process()`, after calculating the `weighted_bands`:
        ```cpp
        double geometric_mean_sum = 0.0;
        double arithmetic_mean_sum = 0.0;
        for (float energy : weighted_bands) {
            geometric_mean_sum += std::log(energy);
            arithmetic_mean_sum += energy;
        }
        double geometric_mean = std::exp(geometric_mean_sum / band_count);
        double arithmetic_mean = arithmetic_mean_sum / band_count;

        features.spectral_flatness = geometric_mean / arithmetic_mean;
        ```
    3.  This feature is great for controlling the "grain" or "noise" parameter in the `PleasureAnimation`.

### Step 3.2: Implement Chroma Features (Chromagram)

-   **Why It Matters (Music Theory):** A chromagram is a 12-element vector representing the intensity of the 12 pitch classes (C, C#, D, etc.) in the music. It tells you what notes and chords are playing, ignoring the octave. This is the key to harmonic-aware visuals.

-   **How to Implement:**
    1.  This is the most complex step. In `FeatureExtractor`, create a mapping that assigns each FFT bin to one of the 12 pitch classes. This requires converting the linear frequency of each bin to a logarithmic MIDI note number.
    2.  For each frame, iterate through the FFT bins. For each bin, add its energy to the corresponding pitch class in a 12-element `chroma` vector.
    3.  Normalize the resulting vector.
    4.  **Benefit**: You can now map this vector to a color wheel. For example, if the 'C' and 'G' elements are strongest, you can tint the scene with colors associated with a C-Major chord, making the visuals harmonically reactive.
    5.  **Performance**: This should be an optional feature, enabled via config, as it adds another loop over the FFT bins.
