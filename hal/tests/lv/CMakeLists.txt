# SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
#
# SPDX-License-Identifier: Apache-2.0

# Add the server executable
add_executable(mmio_server mmio_server.cc)
target_link_libraries(mmio_server PRIVATE libcomm formosa-real.lv)
target_include_directories(mmio_server PRIVATE
                           ${CMAKE_CURRENT_SOURCE_DIR}/../../include
                           ${CMAKE_SOURCE_DIR}
                           ${CMAKE_SOURCE_DIR}/hal/src
                           ${CMAKE_SOURCE_DIR}/hal/src/hal)

# Add the client (test) executable
add_executable(formosa_hal_test formosa_hal_test.cc)
target_link_libraries(formosa_hal_test PRIVATE
  libcomm
  formosa-hal.lv
  gpufw_elf_carr
)
target_include_directories(formosa_hal_test PRIVATE
                           ${CMAKE_SOURCE_DIR}/ext/libcomm/include
                           ${CMAKE_SOURCE_DIR}
                           ${CMAKE_SOURCE_DIR}/hal/src)

add_executable(race_condition_test race_condition_test.cc)
target_link_libraries(race_condition_test PRIVATE libcomm formosa-hal.lv)
target_include_directories(race_condition_test PRIVATE
                           ${CMAKE_SOURCE_DIR}/ext/libcomm/include
                           ${CMAKE_SOURCE_DIR}/include)

# Define the tests using add_test
add_test(NAME hal.lv_test
         COMMAND ${CMAKE_COMMAND} -E env LV_FORMOSA_FAIL_INIT_ONCE=1
         ${CMAKE_CURRENT_SOURCE_DIR}/run_lv_test.sh
         $<TARGET_FILE:mmio_server> $<TARGET_FILE:formosa_hal_test>)
set_property(TEST hal.lv_test PROPERTY LABELS lv hal unit)
# Socket path is unique per run (mktemp); no shared daemon lock required.

add_test(NAME hal.lv_invalid_config_test
         COMMAND ${CMAKE_COMMAND} -E env FORMOSA_HAL_INVALID_CONFIG=1
         LV_FORMOSA_THREADS_PER_WARP=0
         ${CMAKE_CURRENT_SOURCE_DIR}/run_lv_test.sh
         $<TARGET_FILE:mmio_server> $<TARGET_FILE:formosa_hal_test>)
set_property(TEST hal.lv_invalid_config_test PROPERTY LABELS lv hal unit)

add_test(NAME hal.lv_race_test
         COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/run_lv_test.sh
         $<TARGET_FILE:mmio_server> $<TARGET_FILE:race_condition_test>)
set_property(TEST hal.lv_race_test PROPERTY LABELS lv hal unit)
