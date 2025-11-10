# Audio System Design Overview

This document describes how `when` captures audio, transforms it into perceptual
features, and exposes those features to animations. It supplements
`AUDIO_UPGRADE.md` by consolidating the current implementation details in a
single reference.

## Capture and Ingestion

* **AudioEngine** wraps [miniaudio](https://miniaud.io/) to pull samples from
  either the configured capture device or an audio file. The engine always
  delivers interleaved floating-point frames at the requested sample rate and
  channel count.
* Samples are accumulated in a ring buffer so the main loop can read large
  chunks without blocking the callback thread.

## Core DSP Pipeline

1. **Framing & Windowing** – `DspEngine` slices the mono-summed audio into
   overlapping frames (FFT size + hop size configurable via `[dsp]`). Each frame
   is windowed with a Hann taper to minimise spectral leakage.
2. **FFT & Log Binning** – A real FFT converts the frame into magnitude/phase.
   Bins are aggregated into perceptually spaced bands (`dsp.bands`).
3. **Per-Band Flux** – Instantaneous energy deltas are tracked to estimate
   onsets; the raw flux array is forwarded to the feature extractor for reuse.
4. **Feature Extraction** – The frame context is handed to
   `FeatureExtractor::process`, which performs the higher-level perceptual work
   described below.

## FeatureExtractor Responsibilities

By default the extractor applies A-weighting (configurable via
`dsp.apply_a_weighting`) to approximate human loudness perception, then derives a
variety of tiered analyses. Each group can drive different kinds of visuals:

### Tier 0 – Core Energies

* Smoothed bass, mid, treble, and total energies.
* Matching envelope followers with configurable attack/release.
* Instantaneous (unsmoothed) energy taps for snappy reactions.

### Tier 1 – Rhythm Tracking

* Aggregate beat strength and a boolean pulse when the strength crosses the
  configured detection threshold.
* Independent onset detectors for bass/mid/treble to enable selective accents.
* Tempo estimation with phase tracking across both beats and bars, including a
  downbeat flag.

### Tier 2 – Spectral Shape (Toggleable)

* `spectral_centroid` summarises brightness.
* `spectral_flatness` reveals tonal versus noise-like content.
* Controlled by `dsp.enable_spectral_flatness`.

### Tier 3 – Harmonic Content (Toggleable)

* A 12-bin chroma vector aligned to pitch classes C through B.
* `chroma_available` guards consumers when chroma is disabled or insufficient
  data is present.
* Controlled by `dsp.enable_chroma` and bounded by the configured frequency
  range in `FeatureExtractor::Config`.

## Configuration Surface

`when.toml` exposes the key controls you can tune without recompiling:

| Key | Description |
| --- | --- |
| `dsp.fft_size`, `dsp.hop_size`, `dsp.bands` | Frame geometry and band count.
  Larger FFTs yield finer frequency resolution at the cost of latency. |
| `dsp.smoothing_attack`, `dsp.smoothing_release` | Global attack/release
  constants for all band envelopes. They define how quickly smoothed energies
  chase instantaneous changes. |
| `dsp.apply_a_weighting` | Enables perceptual weighting of FFT bins before band aggregation. |
| `dsp.enable_spectral_flatness` | Enables Tier-2 spectral-shape analysis. |
| `dsp.enable_chroma` | Enables Tier-3 chroma analysis. |

When a feature class is disabled, the extractor short-circuits the work and
returns neutral values so downstream consumers stay predictable.

## Consuming Features Safely

Animations receive a fresh `AudioFeatures` struct on every frame. To use it
robustly:

* Prefer the smoothed energies/envelopes for motion and the instantaneous values
  for punchy effects.
* Gate chroma-dependent logic on `chroma_available` and fall back gracefully
  when the flag is false.
* Treat `spectral_flatness` as zero when its toggle is disabled.
* Use the raw `band_flux` span for bespoke onset visualisations—no need to
  recompute flux yourself.

## Extensibility Notes

* `FeatureExtractor::Config` centralises the perceptual tuning knobs. Extend it
  when adding new analyses so everything remains configurable from the main
  application.
* `DspEngine` accepts a populated `FeatureExtractor::Config`, making it easy to
  surface new options without changing the audio loop.
* Keep expensive analyses guarded by configuration flags so installations can
  scale the visualiser to their hardware.

