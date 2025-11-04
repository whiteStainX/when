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
  src/pleasure.cpp
  src/main.cpp
  src/audio_engine.cpp
  src/config.cpp
  src/config/raw_config.cpp
  src/config/value_parsers.cpp
  src/config/animation_config_parser.cpp
  src/plugins.cpp
  src/renderer.cpp
  src/dsp.cpp
  src/animations/ascii_matrix_animation.cpp
  src/animations/animation_manager.cpp
  src/animations/glyph_utils.cpp
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
