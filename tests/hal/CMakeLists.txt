# SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
#
# SPDX-License-Identifier: Apache-2.0

if(lv IN_LIST ENABLE_PROJECTS AND hal IN_LIST ENABLE_PROJECTS)
  find_package(OpenCL REQUIRED)

  add_executable(hal_memory_copy_test hal_memory_copy_test.cc)
  target_link_libraries(hal_memory_copy_test PRIVATE
    libcomm
    formosa-hal.lv
    gpufw_elf_carr
  )
  target_include_directories(hal_memory_copy_test PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/libcomm/include
    ${CMAKE_SOURCE_DIR}/hal/include
  )

  add_executable(hal_reboot_test hal_reboot_test.cc)
  target_link_libraries(hal_reboot_test PRIVATE
    libcomm
    formosa-hal.lv
    gpufw_elf_carr
  )
  target_include_directories(hal_reboot_test PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/libcomm/include
    ${CMAKE_SOURCE_DIR}/hal/include
  )

  add_executable(hal_capture_replay_vecadd ../opencl/vecadd/main.cc)
  target_link_libraries(hal_capture_replay_vecadd PRIVATE OpenCL::OpenCL)
  target_compile_definitions(hal_capture_replay_vecadd PRIVATE
    CL_TARGET_OPENCL_VERSION=120
    CL_HPP_TARGET_OPENCL_VERSION=120
    CL_HPP_MINIMUM_OPENCL_VERSION=120
    CL_HPP_ENABLE_EXCEPTIONS=ON
    CL_HPP_CL_1_2_DEFAULT_BUILD
    KERNEL_PATH="${CMAKE_SOURCE_DIR}/tests/opencl/vecadd/kernel.cl")

  set(TESTING_OUT ${CMAKE_BINARY_DIR}/testing-out)
  file(MAKE_DIRECTORY ${TESTING_OUT})

  add_test(NAME hal.memory_copy
           COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/run_daemon_test.sh
           $<TARGET_FILE:lv>
           ${CMAKE_SOURCE_DIR}/tests/formosa/daemon.lua
           $<TARGET_FILE:hal_memory_copy_test>)
  set_property(TEST hal.memory_copy PROPERTY LABELS lv hal integration)
  set_property(TEST hal.memory_copy PROPERTY RESOURCE_LOCK formosa_daemon)

  add_test(NAME hal.reboot
           COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/run_daemon_test.sh
           $<TARGET_FILE:lv>
           ${CMAKE_SOURCE_DIR}/tests/formosa/daemon.lua
           $<TARGET_FILE:hal_reboot_test>)
  set_property(TEST hal.reboot PROPERTY LABELS lv hal integration)
  set_property(TEST hal.reboot PROPERTY RESOURCE_LOCK formosa_daemon)

  add_test(NAME hal.opencl_capture_replay
           COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/capture_replay_test.sh
           $<TARGET_FILE:lv>
           ${CMAKE_SOURCE_DIR}/tests/opencl/run_opencl.lua
           ${CMAKE_SOURCE_DIR}/tests/opencl/run_opencl_replay.lua
           $<TARGET_FILE:hal_capture_replay_vecadd>
           ${TESTING_OUT}/hal_opencl_capture_replay
           simtix.pipelined_sm)
  set_property(TEST hal.opencl_capture_replay PROPERTY LABELS lv hal integration opencl)
  set_property(TEST hal.opencl_capture_replay PROPERTY WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}")

else()
   message(STATUS "Skipping hal integrations tests since lv or hal is not enabled.")
endif()
