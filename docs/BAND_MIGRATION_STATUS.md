# BAND Sprite Loader Migration Status

## Current compatibility surface
- **Legacy loader**: `when::animations::band::load_sprite_set` remains available in `src/animations/band/sprite_types.cpp` for the state-driven asset layout.
- **Compatibility shim**: `SpritePlayer::set_sequence(const std::vector<SpriteFrame>* sequence)` in `src/animations/band/sprite_types.cpp` bridges existing callers that still keep their frames in `SpriteSet` containers.

## Known dependents of `SpriteSet`
- `extra/sprite_player_harness.cpp` – loads a `SpriteSet` from the legacy `assets/sprites/<member>/<state>.txt` files to drive the manual playback harness.
- `tests/band_sprite_assets_test.cpp` – continues to assert that every band member exposes the legacy state files by calling `load_sprite_set` after exercising the sequence loaders.

No other in-tree code consumes the legacy loader; production band animation code paths already work with `SpriteSequence`.

## Readiness criteria for enabling directory layout by default
Flip `WHEN_BAND_ENABLE_DIRECTORY_LAYOUT` to `1` only after:
1. **Harness update** – Refactor `extra/sprite_player_harness.cpp` to load directory-based sequences (e.g., `load_sprite_sequence`) so it exercises the new layout when developers test animations manually.
2. **Asset verification** – Update `tests/band_sprite_assets_test.cpp` to cover only the sequence loaders. The test should fail if a member lacks directory-organized frames instead of depending on the stateful set loader.
3. **Asset layout parity** – Ensure every member under `assets/sprites/` owns a directory of per-frame files with the same coverage as the old `SpriteSet` (idle/normal/fast/spotlight, plus optional `spotlight_hi`).
4. **Documentation sweep** – Replace any contributor docs that still reference the `SpriteSet` contract with guidance for the directory layout so new content is produced in the modern format.

## Criteria for removing the legacy path entirely
After the flag ships enabled by default and CI has run successfully across a full release cycle:
- Delete `SpriteSet` and its loader once no out-of-tree tooling relies on it (confirm via a repo-wide search before removal).
- Drop the compatibility `SpritePlayer::set_sequence` overload that accepts `std::vector<SpriteFrame>`.
- Remove the `WHEN_BAND_ENABLE_DIRECTORY_LAYOUT` preprocessor guard and simplify `load_sprite_sequence` to the directory-first implementation.

Document the removals in the release notes for the next Phase 2 milestone so downstream consumers know the stateful layout is no longer supported.
