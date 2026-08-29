# SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
#
# SPDX-License-Identifier: Apache-2.0

if(lv IN_LIST ENABLE_PROJECTS)
  add_executable(ipc_test ipc_test.cc)
  target_link_libraries(ipc_test PRIVATE libcomm)
  add_lv_test_script(ipc_test ipc_test.lua ARGS $<TARGET_FILE:ipc_test>)
  set_property(TEST ipc_test APPEND PROPERTY LABELS "integration")
endif()
