# BAND Animation Delivery Plan

This plan decomposes the band animation into tightly scoped phases. Each phase ends with
explicit validation criteria so we can halt, adjust thresholds, or reprioritize before
investing in subsequent work.

## Phase 0 – Foundations & Asset Loader
1. **Sprite container types**
   Implement `SpriteFrame`, `SpriteSet`, and `SpritePlayer` plus serialization helpers tied to the
   legacy state-driven asset layout.
   _Validation_: Unit-test the loader with representative sprite files (idle/normal/fast)
   ensuring width/height consistency checks fire on malformed assets.
2. **Standalone render harness**  
   Build a minimal console app that instantiates a single `SpritePlayer`, drives it at
   target FPS, and blits onto a dedicated `ncplane`.  
   _Validation_: Capture a 30-second terminal session to confirm UTF-8 glyphs and timing
   remain stable; verify via CI artifact or manual review.
3. **Assets staging**
   Establish `assets/sprites/<member>/<state>.txt` layout with placeholder art, covering the
   legacy `SpriteSet` contract that Phase 1.5 will retire.
   _Validation_: `band_sprite_assets_test` enumerates `assets/sprites/<member>` directories,
   loading each required state file and failing CI if any asset is missing or empty.

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
   believable ranges (no NaNs/inf); the `band-feature-tap-logger` now derives its tap
   configuration from the live `FeatureExtractor` settings so telemetry mirrors runtime
   behavior.

## Phase 1.5 – Sequence-Based Asset Migration
1. **Introduce `SpriteSequence` containers**
   Replace `SpriteSet` usage in the loader and tests with a sequence-oriented container that
   models a loop of frames without `MemberState` indirection. Update serialization helpers to
   output `std::vector<SpriteFrame>` instances and keep compatibility shims where necessary so
   existing callers can migrate incrementally.
   _Validation_: Loader unit tests cover both the legacy `SpriteSet` path and the new
   `SpriteSequence` path. Legacy coverage can be marked deprecated but must stay green until the
   migration finishes.
2. **Directory-based loader update**
   Teach the loader to read from `assets/sprites/<member>/<frame>.txt`, sorting filenames
   alphabetically into a `SpriteSequence`. Maintain the legacy layout behind a feature flag or
   compile-time toggle until all consumers switch over.
   _Validation_: Fixture assets exist for both layouts, and tests assert the correct frame counts
   and ordering for each. CI fails if either layout regresses while the migration flag is enabled.
3. **Migration readiness review**
   Add documentation or inline notes explaining the cut-over plan, including which modules still
   depend on the stateful loader. Explicitly call out when it is safe to delete the legacy path so
   Phase 2 work can assume the sequence-only world.
   _Validation_: Design review sign-off confirming no runtime path still requires the stateful
   layout.

## Phase 2 – Direct-Drive Speed Control Prototype

**Goal:** Create a working, single-panel animation that loads a `SpriteSequence` of per-frame
sprite files from a directory and directly maps audio features to the sequence's playback speed.

1.  **Asset Loading for a Single Sequence**
    Consume the `SpriteSequence` emitted by Phase 1.5. When given a directory path (e.g.,
    `assets/sprites/drummer/`), the loader must treat **each `.txt` file within it as a single
    frame**, sort the files alphabetically, and return the ordered `SpriteSequence` that loops for
    that character.
    _Validation_: A unit test (`band_sprite_loader_test.cpp`) confirms that loading the
    `assets/sprites/drummer/` directory results in a `SpriteSequence` containing exactly 4
    `SpriteFrame` objects, with the content of each matching the respective `.txt` file.

2.  **Instrument Heuristics Implementation**  
    Implement the `InstrumentHeuristics` for the **Drummer** role. The `activity_score` method will be the primary output, translating audio features (like `bass_beat` and `treble_beat`) into a continuous `[0,1]` activity level.
    _Validation_: A unit test feeds recorded features into the heuristic. Assert that the Drummer's `activity_score` is highest during percussion-heavy audio passages.

3.  **Variable-Speed Playback**
    Implement the `SpritePlayer`. Its `update` method accepts the `activity_score` and computes the
    instantaneous frame cadence via linear interpolation: `current_fps = lerp(min_fps, max_fps,
    activity_score)`. Increment an internal accumulator by `delta_time`, and when the accumulator
    exceeds `threshold = 1 / current_fps`, advance the frame and subtract the threshold. This keeps
    the animation speed directly proportional to the activity score while clamping it between
    `min_fps` and `max_fps`.
    _Validation_: In a standalone test harness, pass in `activity_score` values of 0.0, 0.5, and 1.0
    over a fixed 10-second run. Assert that the player produces `floor(min_fps * 10)`,
    `floor(lerp(min_fps, max_fps, 0.5) * 10)`, and `floor(max_fps * 10)` frame advances,
    respectively. Expose default `min_fps` and `max_fps` via configuration hooks so Phase 5 can wire
    them through the user-facing config.

4.  **Simplified PanelController Prototype**  
    Create the `PanelController` for the Drummer. In its `init` method, it will:
    a.  Load the single animation sequence from the `assets/sprites/drummer/` directory.
    b.  Pass this sequence to its internal `SpritePlayer`.
    In its `update` loop, it will:
    a.  Call `InstrumentHeuristics` to get the current `activity_score`.
    b.  Pass this score directly to the `SpritePlayer::update` method.
    _Validation_: Manual run of the visualizer in "single panel mode." The Drummer animation, composed of the 4 loaded frames, should be almost static during silence, play at a moderate speed during normal passages, and loop very quickly during intense drum fills. The motion is continuous and fluid.

## Phase 3 – Full Band Layout & Independent Animation

**Goal:** Display all four band members on screen, each animating independently based on their own heuristics.

1.  **BandDirectorAnimation Integration**
    Implement the `BandDirectorAnimation` class. Its primary job is to instantiate four `PanelController`s (one for each role using the Phase 2 heuristics), compute the 2x2 panel layout, and create the `ncplane` for each panel.
    _Validation_: On launch, all four panels appear with their titles and borders, each showing their respective character in the `idle` state. The layout adapts correctly to terminal resizing.

2.  **Parallel Heuristics Update**  
    In `BandDirectorAnimation::update`, iterate through all four `PanelController`s and call their individual `update` methods, passing in the global `FeatureView`. Each panel will run its own independent logic.
    _Validation_: Manual run with music. All four members animate at the same time, but with different patterns. The Bassist should be more active during bass-heavy sections, the Drummer during percussive sections, etc. There is no spotlighting yet.

## Phase 4 – Advanced State & Spotlight Orchestration *(Optional Follow-up)*

**Goal:** Re-introduce state management to create more deliberate, less "flappy" animations and implement the coordinated spotlight feature. This phase becomes a stretch objective if the stateless smoothing from Phase 2 already yields satisfactory responsiveness.

1.  **Implement `InstrumentStateMachine`**  
    Now, build the `InstrumentStateMachine` as originally designed, complete with hysteresis timers (`idle_hold_sec`, `fast_hold_sec`). Refactor `PanelController` to use the state machine instead of the direct `if/else` logic. The FSM will now be responsible for deciding the `MemberState`.
    _Validation_: Manual run. The animations should now feel more "sticky." A character entering the `fast` state will remain there for a minimum duration, even if the audio energy dips for a moment, preventing frantic switching.

2.  **Spotlight Scoring & Orchestration**  
    Implement the `spotlight_score` method in `InstrumentHeuristics`. In `BandDirectorAnimation`, implement the `manage_spotlight` policy. On each frame, it will:
    a.  Check the `spotlight_score` from all four panels.
    b.  If no one is in the spotlight, grant it to the panel with the highest score above a threshold, aligned to the next beat (`features.beat_phase`).
    c.  If a panel is already in the spotlight, keep it there until its `spotlight_min_bars` duration has elapsed.
    _Validation_: Use a debug overlay to print the spotlight scores for each member. Verify that only one member enters the spotlight at a time and that the lock is held for the correct duration.

3.  **Zoom Assets & Transitions**  
    Update `PanelController` to use the optional `spotlight_hi` sprite set when its state machine enters the `Spotlight` state.
    _Validation_: Manual check that spotlight entry correctly triggers the switch to the higher-resolution asset.

## Phase 5 – Configuration & Robustness

1.  **Configuration Parsing**  
    Extend `AppConfig` / `AnimationConfig` and `src/config.cpp` to read `[visual.band]` plus per-member tables from `when.toml`.
    _Validation_: Add unit tests that load fixture TOML files (complete, partial, malformed) and confirm defaults or errors fire as documented.

2.  **Runtime Toggles & Degradation**  
    Implement flags for spotlight enablement, border rendering, and fallback animation FPS when frame budget is tight.
    _Validation_: Stress test by artificially reducing frame budget; ensure toggles kick in and telemetry logs the mode change.

3.  **End-to-end Acceptance Run**  
    Execute the full visualizer with real audio, verifying heuristics degrade gracefully when chroma is disabled.
    _Validation_: QA checklist with screenshots/video captures for all four instruments in all states.

## Phase 6 – Polish & Knowledge Transfer

1.  **Documentation Pass**  
    Update `BAND.md`, `when.toml` comments, and inline code docs to reflect shipped behavior.
    _Validation_: Peer review sign-off plus lint/CI run with documentation build (if any).

2.  **Support Tooling**  
    Add developer shortcuts (hotkeys, debug overlays, logging toggles) to aid future tuning.
    _Validation_: Demonstrate the tooling in a walkthrough session recorded for the team wiki.

3.  **Post-launch Monitoring Hooks**  
    Emit optional metrics (e.g., spotlight uptime, average FPS) to the existing telemetry channel or logs.
    _Validation_: Confirm metrics appear during a 10-minute soak test and can be graphed via the standard observability stack.
