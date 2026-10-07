# SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
#
# SPDX-License-Identifier: Apache-2.0

if(lv IN_LIST ENABLE_PROJECTS AND hal IN_LIST ENABLE_PROJECTS)
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
    ${ILHA_GENERATED_DIR}
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
    ${ILHA_GENERATED_DIR}
  )

  add_test(NAME hal.memory_copy
           COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/run_daemon_test.sh
           $<TARGET_FILE:lv>
           ${CMAKE_SOURCE_DIR}/ilha/daemon.lua
           $<TARGET_FILE:hal_memory_copy_test>)
  set_property(TEST hal.memory_copy PROPERTY LABELS lv hal integration)
  set_property(TEST hal.memory_copy PROPERTY RESOURCE_LOCK formosa_daemon)

  add_test(NAME hal.large_sm_capabilities
           COMMAND ${CMAKE_COMMAND} -E env
           FORMOSA_DAEMON_CONFIG=large_sm
           FORMOSA_EXPECTED_MAX_THREADS=1024
           ${CMAKE_CURRENT_SOURCE_DIR}/run_daemon_test.sh
           $<TARGET_FILE:lv>
           ${CMAKE_SOURCE_DIR}/ilha/daemon.lua
           $<TARGET_FILE:hal_memory_copy_test>)
  set_property(TEST hal.large_sm_capabilities PROPERTY LABELS lv hal integration)
  set_property(TEST hal.large_sm_capabilities PROPERTY RESOURCE_LOCK formosa_daemon)

  add_test(NAME hal.reboot
           COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/run_daemon_test.sh
           $<TARGET_FILE:lv>
           ${CMAKE_SOURCE_DIR}/ilha/daemon.lua
           $<TARGET_FILE:hal_reboot_test>)
  set_property(TEST hal.reboot PROPERTY LABELS lv hal integration)
  set_property(TEST hal.reboot PROPERTY RESOURCE_LOCK formosa_daemon)

else()
   message(STATUS "Skipping hal integrations tests since lv or hal is not enabled.")
endif()
