# BAND Animation Delivery Plan

This plan decomposes the band animation into tightly scoped phases. Each phase ends with
explicit validation criteria so we can halt, adjust thresholds, or reprioritize before
investing in subsequent work.

## Phase 0 – Foundations & Asset Loader
1. **Sprite container types**  
   Implement `SpriteFrame`, `SpriteSet`, and `SpritePlayer` plus serialization helpers.  
   _Validation_: Unit-test the loader with representative sprite files (idle/normal/fast)
   ensuring width/height consistency checks fire on malformed assets.
2. **Standalone render harness**  
   Build a minimal console app that instantiates a single `SpritePlayer`, drives it at
   target FPS, and blits onto a dedicated `ncplane`.  
   _Validation_: Capture a 30-second terminal session to confirm UTF-8 glyphs and timing
   remain stable; verify via CI artifact or manual review.
3. **Assets staging**  
   Establish `assets/sprites/<member>/<state>.txt` layout with placeholder art.  
   _Validation_: Loader integration test enumerates the directory tree and reports missing
   states; CI must pass with at least idle/normal sequences per member.

## Phase 1 – Instrument Feature Taps
1. **FeatureView assembly**  
   Add a translator that condenses `AudioFeatures` into the `FeatureView` struct described
   in `BAND.md` (aggregate `band_flux`, compute chroma dominance, normalize centroid).  
   _Validation_: Unit-test using recorded `AudioFeatures` frames to ensure flux aggregation
   matches expected low/mid/high buckets and chroma dominance stays within [0,1].
2. **Telemetry capture**  
   Extend the existing logging or add a developer command to dump `FeatureView` values for
   short clips.  
   _Validation_: Run a known track through the analyzer and inspect the CSV/log output for
   believable ranges (no NaNs/inf); sign off before wiring heuristics.

## Phase 2 – Single Panel Prototype
1. **Instrument heuristics implementation**  
   Code `InstrumentHeuristics` and `InstrumentStateMachine` for one role (e.g., Drummer)
   using thresholds from `BAND.md`.  
   _Validation_: Feed recorded features into a deterministic test harness; assert state
   transitions follow the expected timeline (idle → normal → fast).
2. **PanelController skeleton**  
   Create `PanelController` with init/update/render pipeline and integrate the single role.  
   _Validation_: Manual run of the visualizer in “single panel mode”; confirm frame rate is
   stable and debug overlay shows activity/spotlight scores.

## Phase 3 – Full Band Layout & Spotlight
1. **BandDirectorAnimation integration**  
   Instantiate four `PanelController`s, compute the 2×2 layout, and manage per-panel planes.  
   _Validation_: Render on minimum supported terminal size; ensure borders/titles fit and
   there are no overlaps or clipping.
2. **Spotlight orchestration**  
   Implement beat-aligned spotlight policy with bar-length holds and priority rules.  
   _Validation_: Use synthetic feature sequences to assert only one spotlight is active at
   a time and that lock timers respect the configured minimum bars.
3. **Zoom assets + transitions**  
   Wire optional `spotlight_hi` sprite sets and plane translation for subtle motion.  
   _Validation_: Manual check that spotlight entry/exit has no flicker and higher-resolution
   assets line up with panel boundaries.

## Phase 4 – Configuration & Robustness
1. **Configuration parsing**  
   Extend `AppConfig` / `AnimationConfig` and `src/config.cpp` to read `[visual.band]` plus
   per-member tables.  
   _Validation_: Add unit tests that load fixture TOML files (complete, partial, malformed)
   and confirm defaults or errors fire as documented.
2. **Runtime toggles & degradation**  
   Implement flags for spotlight enablement, border rendering, and fallback animation FPS
   when frame budget is tight.  
   _Validation_: Stress test by artificially reducing frame budget; ensure toggles kick in
   and telemetry logs the mode change.
3. **End-to-end acceptance run**  
   Execute the full visualizer with real audio, verifying heuristics degrade gracefully
   when chroma is disabled.  
   _Validation_: QA checklist with screenshots/video captures for all four instruments in
   idle/normal/fast/spotlight states.

## Phase 5 – Polish & Knowledge Transfer
1. **Documentation pass**  
   Update `BAND.md`, `when.toml` comments, and inline code docs to reflect shipped behavior.  
   _Validation_: Peer review sign-off plus lint/CI run with documentation build (if any).
2. **Support tooling**  
   Add developer shortcuts (hotkeys, debug overlays, logging toggles) to aid future tuning.  
   _Validation_: Demonstrate the tooling in a walkthrough session recorded for the team wiki.
3. **Post-launch monitoring hooks**  
   Emit optional metrics (e.g., spotlight uptime, average FPS) to the existing telemetry
   channel or logs.  
   _Validation_: Confirm metrics appear during a 10-minute soak test and can be graphed via
   the standard observability stack.
