# SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
#
# SPDX-License-Identifier: Apache-2.0

if(lv IN_LIST ENABLE_PROJECTS)
  add_executable(ipc_test ipc_test.cc)
  target_link_libraries(ipc_test PRIVATE libcomm)
  add_lv_test_script(ipc_test ipc_test.lua ARGS $<TARGET_FILE:ipc_test>)
  set_property(TEST ipc_test APPEND PROPERTY LABELS "integration")
  add_executable(ipc_synchronous_host synchronous_host.cc)
  target_link_libraries(ipc_synchronous_host PRIVATE libcomm)
  foreach(delay 0 20)
    add_lv_test_script(ipc.synchronous.${delay} synchronous.lua
      ARGS $<TARGET_FILE:ipc_synchronous_host> ${delay})
    set_tests_properties(ipc.synchronous.${delay} PROPERTIES TIMEOUT 20)
  endforeach()
  add_lv_test_script(ipc.synchronous.disconnect synchronous.lua
    ARGS $<TARGET_FILE:ipc_synchronous_host> 0 synchronous disconnect)
  set_tests_properties(ipc.synchronous.disconnect PROPERTIES TIMEOUT 10)
endif()
