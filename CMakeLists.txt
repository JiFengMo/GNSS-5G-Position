cmake_minimum_required(VERSION 3.16)
project(BDS5GPositioning VERSION 0.1.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

option(BDS5G_BUILD_TESTS "Build offline unit tests" ON)
option(BDS5G_BUILD_LEGACY_NR "Build the UHD-dependent legacy 5G tree" OFF)

add_library(bds5g_core STATIC
  src/common/observation_io.c
  src/bds_b1c/b1c_code.c
  src/bds_b1c/b1c_acquisition.c
  src/bds_b1c/b1c_tracking.c
  src/bds_b1c/b1c_navigation.c
  src/nr5g/nr5g_adapter.c
  src/fusion/fusion_wls.c
)

target_include_directories(bds5g_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

if(MSVC)
  target_compile_options(bds5g_core PRIVATE /W4)
else()
  target_compile_options(bds5g_core PRIVATE -Wall -Wextra -Wpedantic)
  target_link_libraries(bds5g_core PUBLIC m)
endif()

add_executable(bds5g-positioning apps/bds5g_positioning.c)
target_link_libraries(bds5g-positioning PRIVATE bds5g_core)

add_executable(bds-b1c-receiver apps/bds_b1c_receiver.c)
target_link_libraries(bds-b1c-receiver PRIVATE bds5g_core)

add_executable(nr5g-toa-receiver apps/nr5g_toa_receiver.c)
target_link_libraries(nr5g-toa-receiver PRIVATE bds5g_core)

if(BDS5G_BUILD_TESTS)
  enable_testing()
  add_executable(test-fusion-wls tests/test_fusion_wls.c)
  target_link_libraries(test-fusion-wls PRIVATE bds5g_core)
  add_test(NAME fusion_wls COMMAND test-fusion-wls)
endif()

# The original NR-TOA implementation requires UHD and a Linux-like host.
# Keep it opt-in so the portable signal-processing library has no RF dependency.
if(BDS5G_BUILD_LEGACY_NR)
  add_subdirectory(5G legacy_nr_build)
endif()
