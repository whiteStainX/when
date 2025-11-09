# BAND Sprite Loader Migration Status

## Current compatibility surface
- **Legacy loader**: `when::animations::band::load_sprite_set` is still present for out-of-tree tooling but no longer exercised by
  any repository binaries or tests.
- **Directory-first API**: `load_sprite_sequence_from_directory` is now the canonical entry point; harnesses and tests drive it
  exclusively.
- **Compatibility shim**: `SpritePlayer::set_sequence(const std::vector<SpriteFrame>* sequence)` remains available to ease the
  eventual removal of `SpriteSet`, but in-tree callers now provide a `SpriteSequence`.

## In-tree usage snapshot
- `extra/sprite_player_harness.cpp` – loads a guitarist sequence from `assets/sprites/guitarist/` and plays it back at a fixed
  FPS for manual testing.
- `tests/band_sprite_assets_test.cpp` – enumerates each member directory, asserts the absence of legacy `idle/normal/fast/spotlight`
  files, and validates that the per-frame sequence has consistent dimensions.

No other in-tree code consumes the legacy loader; production band animation code paths already work with `SpriteSequence`.

## Readiness criteria for enabling directory layout by default
Flip `WHEN_BAND_ENABLE_DIRECTORY_LAYOUT` to `1` only after:
1. **Legacy audit** – Confirm no external tooling depends on `SpriteSet` before enabling the directory loader globally.
2. **Config exposure** – Wire the directory-based asset paths through `when.toml` so runtime configuration never references
   state-file names.
3. **CI bake time** – Run the band visualizer across representative clips to ensure the direct-drive playback produces stable
   timing without the old state gates.

## Criteria for removing the legacy path entirely
After the flag ships enabled by default and CI has run successfully across a full release cycle:
- Delete `SpriteSet` and its loader once no out-of-tree tooling relies on it (confirm via a repo-wide search before removal).
- Drop the compatibility `SpritePlayer::set_sequence` overload that accepts `std::vector<SpriteFrame>`.
- Remove the `WHEN_BAND_ENABLE_DIRECTORY_LAYOUT` preprocessor guard and simplify `load_sprite_sequence` to the directory-first
  implementation.

Document the removals in the release notes for the next Phase 2 milestone so downstream consumers know the stateful layout is no
longer supported.
