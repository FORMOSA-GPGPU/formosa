# SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
#
# SPDX-License-Identifier: Apache-2.0

# Builds NAME.S without common/start.S so each barrier sits at a fixed PC, and
# matches the simulator's report against PASS_REGEX (and FAIL_REGEX, if given).
# The report is matched instead of the exit code because LV_FATAL ends the
# deadlock case.
function(add_kernel_sim_barrier_test NAME PASS_REGEX)
  cmake_parse_arguments(BARRIER "" "FAIL_REGEX" "" ${ARGN})
  set(target kernel-sim.barrier.${NAME})
  add_executable(${target} ${NAME}.S)
  target_link_options(${target} PRIVATE
    -fuse-ld=lld
    -static
    -T ${LINKER_SCRIPT}
    -nostdlib
  )
  set_target_properties(${target} PROPERTIES
    LINK_DEPENDS ${LINKER_SCRIPT}
    OUTPUT_NAME barrier.${NAME}.kernel-sim.elf
  )

  foreach(sm ${KERNEL_SIM_SMS})
    set(test_name kernel-sim.barrier.${NAME}.${sm})
    add_lv_test_script(${test_name} barrier.lua
      LABELS kernel-sim barrier
      ARGS $<TARGET_FILE:${target}> --sm ${sm}
    )
    set_tests_properties(${test_name} PROPERTIES
      PASS_REGULAR_EXPRESSION "${PASS_REGEX}"
      TIMEOUT 60
    )
    if(BARRIER_FAIL_REGEX)
      set_tests_properties(${test_name} PROPERTIES
        FAIL_REGULAR_EXPRESSION "${BARRIER_FAIL_REGEX}"
      )
    endif()
  endforeach()
endfunction()

# Warp 0 exits; warp 1 waits at 0x11008.
add_kernel_sim_barrier_test(deadlock
  "barrier deadlock in WG slot 0 .*: 1 of [0-9]+ warps exited .*warp@pc: 1@0x11008"
)

# Lanes that exited count as arrived; nothing is reported.
add_kernel_sim_barrier_test(exited-lanes "Kernel completed"
  FAIL_REGEX "different barriers|barrier deadlock"
)

# Lanes of one warp wait at 0x1100c and 0x11010 in the same round.
add_kernel_sim_barrier_test(divergent-lanes
  "has lanes waiting at different barriers \\(pc 0x110(10 and 0x1100c|0c and 0x11010)\\).*Kernel completed"
)

# Warp 0 waits at 0x11008 while warp 1 waits at 0x11010.
add_kernel_sim_barrier_test(divergent-warps
  "resumes warps waiting at different barriers \\(warp@pc: 0@0x11008, 1@0x11010.*Kernel completed"
)
