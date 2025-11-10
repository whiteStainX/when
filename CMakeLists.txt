cmake_minimum_required(VERSION 3.16)
project(when LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)


# --- notcurses via pkg-config ---
find_package(PkgConfig REQUIRED)
pkg_check_modules(NOTCURSES QUIET IMPORTED_TARGET notcurses)
if (NOT NOTCURSES_FOUND)
  pkg_check_modules(NOTCURSES REQUIRED IMPORTED_TARGET notcurses-core)
endif()

# --- sources ---
set(WHEN_SOURCES
  src/ConfigLoader.cpp
  src/audio_engine.cpp
  src/main.cpp
  src/audio_engine.cpp
  src/config.cpp
  src/config/raw_config.cpp
  src/config/value_parsers.cpp
  src/config/animation_config_parser.cpp
  src/plugins.cpp
  src/renderer.cpp
  src/audio/feature_extractor.cpp
  src/dsp.cpp
  src/animations/ascii_matrix_animation.cpp
  src/animations/space_rock_animation.cpp
  src/animations/pleasure_animation.cpp
  src/animations/animation_manager.cpp
  src/animations/glyph_utils.cpp
  src/animations/band/sprite_types.cpp
  src/animations/band/feature_taps.cpp
  external/kissfft/kiss_fft.c
)

add_executable(when ${WHEN_SOURCES})

# Add a custom target to track changes in when.toml
add_custom_target(config_dependency ALL DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/when.toml)
add_dependencies(when config_dependency)

# --- includes ---
target_include_directories(when PRIVATE
  src
  external/cxxopts
  external/tomlplusplus
  external/miniaudio
  external/kissfft
)

# --- link notcurses (and its transitive deps) ---
target_link_libraries(when PRIVATE PkgConfig::NOTCURSES)

add_executable(feature_extractor_sanity
  extra/feature_extractor_sanity.cpp
  src/audio/feature_extractor.cpp
)

target_include_directories(feature_extractor_sanity PRIVATE
  src
)

add_executable(sprite_player_harness
  extra/sprite_player_harness.cpp
  src/animations/band/sprite_types.cpp
)

target_include_directories(sprite_player_harness PRIVATE
  src
)

target_link_libraries(sprite_player_harness PRIVATE PkgConfig::NOTCURSES)

enable_testing()

add_executable(band_sprite_loader_test
  tests/band_sprite_loader_test.cpp
  src/animations/band/sprite_types.cpp
  src/animations/band/feature_taps.cpp
)

target_include_directories(band_sprite_loader_test PRIVATE
  src
)

add_test(NAME band_sprite_loader_test COMMAND band_sprite_loader_test)

add_executable(band_feature_taps_test
  tests/band_feature_taps_test.cpp
  src/animations/band/feature_taps.cpp
)

target_include_directories(band_feature_taps_test PRIVATE
  src
)

add_test(NAME band_feature_taps_test COMMAND band_feature_taps_test)

add_executable(audio_engine_stream_test
  tests/audio_engine_stream_test.cpp
  src/audio_engine.cpp
)

target_include_directories(audio_engine_stream_test PRIVATE
  src
  external/miniaudio
)

add_test(NAME audio_engine_stream_test COMMAND audio_engine_stream_test)

add_executable(feature_extractor_weighting_test
  tests/feature_extractor_weighting_test.cpp
  src/audio/feature_extractor.cpp
)

target_include_directories(feature_extractor_weighting_test PRIVATE
  src
)

add_test(NAME feature_extractor_weighting_test COMMAND feature_extractor_weighting_test)

