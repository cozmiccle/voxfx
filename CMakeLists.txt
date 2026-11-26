cmake_minimum_required(VERSION 3.22)
project(voxfx VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

option(VOXFX_BUILD_TESTS "Build unit tests" ON)
option(VOXFX_BUILD_GUI   "Build the Qt GUI" ON)

find_package(PkgConfig REQUIRED)
pkg_check_modules(PIPEWIRE REQUIRED IMPORTED_TARGET libpipewire-0.3)
pkg_check_modules(SNDFILE  REQUIRED IMPORTED_TARGET sndfile)
pkg_check_modules(SOXR     REQUIRED IMPORTED_TARGET soxr)
pkg_check_modules(EBUR128  REQUIRED IMPORTED_TARGET libebur128)
pkg_check_modules(EVDEV    REQUIRED IMPORTED_TARGET libevdev)
pkg_check_modules(ALSA     REQUIRED IMPORTED_TARGET alsa)
pkg_check_modules(RNNOISE  IMPORTED_TARGET rnnoise)   # optional

find_package(Qt6 REQUIRED COMPONENTS Core Network)
if(VOXFX_BUILD_GUI)
  find_package(Qt6 REQUIRED COMPONENTS Widgets)
endif()
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

# ---- core library: everything except the GUI, so tests can link it ----------
add_library(voxfx_core STATIC
  src/dsp/rnnoise_wrap.cpp
  src/dsp/voice_chain.cpp
  src/sfx/clip_loader.cpp
  src/sfx/voice_pool.cpp
  src/sfx/recorder.cpp
  src/engine/engine.cpp
  src/input/binding_manager.cpp
  src/input/evdev_backend.cpp
  src/input/midi_backend.cpp
  src/sources/link_resolver.cpp
  src/sources/link_importer.cpp
  src/sources/library.cpp
  src/common/settings.cpp
)
target_include_directories(voxfx_core PUBLIC src)
target_link_libraries(voxfx_core PUBLIC
  PkgConfig::PIPEWIRE PkgConfig::SNDFILE PkgConfig::SOXR PkgConfig::EBUR128
  PkgConfig::EVDEV PkgConfig::ALSA Qt6::Core Qt6::Network)
if(RNNOISE_FOUND)
  target_link_libraries(voxfx_core PUBLIC PkgConfig::RNNOISE)
  target_compile_definitions(voxfx_core PUBLIC VOXFX_HAVE_RNNOISE=1)
  message(STATUS "RNNoise: enabled")
else()
  message(STATUS "RNNoise: not found, noise suppression falls back to gate only")
endif()
target_compile_options(voxfx_core PRIVATE -Wall -Wextra -Wno-missing-field-initializers)

# ---- app ----------------------------------------------------------------------
if(VOXFX_BUILD_GUI)
  add_executable(voxfx
    src/main.cpp
    src/app.cpp
    src/ui/main_window.cpp
    src/ui/meter_widget.cpp
    src/ui/bind_dialog.cpp
    src/ui/devices_dialog.cpp
    src/ui/theme.cpp
    src/ui/pad_grid.cpp
    src/ui/sound_dialogs.cpp
    packaging/voxfx.qrc
  )
  target_compile_definitions(voxfx PRIVATE VOXFX_VERSION="${PROJECT_VERSION}")
  # Portable bundle (tools/make_portable.sh): look for libraries in ../lib first.
  # DT_RPATH (not RUNPATH) so it also applies to libraries loaded by Qt plugins.
  option(VOXFX_PORTABLE "Build for the self-contained portable bundle" OFF)
  if(VOXFX_PORTABLE)
    set_target_properties(voxfx PROPERTIES INSTALL_RPATH "\$ORIGIN/../lib" BUILD_WITH_INSTALL_RPATH ON)
    target_link_options(voxfx PRIVATE "LINKER:--disable-new-dtags")
  endif()
  target_link_libraries(voxfx PRIVATE voxfx_core Qt6::Widgets)
  target_compile_options(voxfx PRIVATE -Wall -Wextra -Wno-missing-field-initializers)
  install(TARGETS voxfx RUNTIME DESTINATION bin)
  install(FILES packaging/voxfx.desktop DESTINATION share/applications)
  install(FILES packaging/icons/voxfx.svg DESTINATION share/icons/hicolor/scalable/apps)
  install(FILES packaging/icons/voxfx-256.png DESTINATION share/icons/hicolor/256x256/apps RENAME voxfx.png)
  install(FILES packaging/icons/voxfx-64.png DESTINATION share/icons/hicolor/64x64/apps RENAME voxfx.png)
endif()

# ---- headless smoke-test tool (engine only, no Qt GUI) -------------------------
add_executable(voxfx-engine-smoke tools/engine_smoke.cpp)
target_link_libraries(voxfx-engine-smoke PRIVATE voxfx_core)

# ---- tests --------------------------------------------------------------------
if(VOXFX_BUILD_TESTS)
  find_package(Catch2 3 REQUIRED)
  include(CTest)
  include(Catch)
  add_executable(voxfx_tests
    tests/test_dsp.cpp
    tests/test_queue.cpp
    tests/test_voice_pool.cpp
    tests/test_bindings.cpp
    tests/test_link_resolver.cpp
    tests/test_clip_loader.cpp
    tests/test_recorder.cpp
  )
  target_link_libraries(voxfx_tests PRIVATE voxfx_core Catch2::Catch2WithMain)
  target_compile_definitions(voxfx_tests PRIVATE
    VOXFX_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures")
  catch_discover_tests(voxfx_tests)
endif()
