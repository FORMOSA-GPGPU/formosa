# SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
#
# SPDX-License-Identifier: Apache-2.0

add_executable(lv-unit-address-router
  address_router_test.cc
  ${formosa_SOURCE_DIR}/lv/src/lv/bindings/nic/address_router.cc
)
target_compile_features(lv-unit-address-router PRIVATE cxx_std_17)
target_include_directories(lv-unit-address-router PRIVATE ${formosa_SOURCE_DIR})
target_link_libraries(lv-unit-address-router PRIVATE liblv)
add_test(NAME lv.unit.nic.protocol COMMAND lv-unit-address-router)
foreach(case empty zero overflow overlap masters)
  add_test(NAME lv.unit.nic.invalid-${case} COMMAND lv-unit-address-router ${case})
endforeach()
