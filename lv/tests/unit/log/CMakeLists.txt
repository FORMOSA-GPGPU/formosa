# SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
#
# SPDX-License-Identifier: Apache-2.0

# Native unit test for the lv::log seam (drives the C++ functions/macros
# directly; captures to a private file sink and asserts).
add_executable(lv_log_test log_test.cc)
target_link_libraries(lv_log_test PRIVATE liblv)

add_test(NAME lv.unit.log COMMAND lv_log_test)
set_tests_properties(lv.unit.log PROPERTIES
  LABELS "lv;unit"
  PASS_REGULAR_EXPRESSION "ALL_OK")
