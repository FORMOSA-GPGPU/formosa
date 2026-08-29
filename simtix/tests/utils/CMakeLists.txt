# SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
#
# SPDX-License-Identifier: Apache-2.0

set(LATENCY_LIST 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16)
set(THROUGHPUT_INV_LIST 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16)
foreach(lat ${LATENCY_LIST})
  foreach(thi ${THROUGHPUT_INV_LIST})
    math(EXPR rem "${lat} % ${thi}")
    if(rem EQUAL 0)
      set(test_name "delay_queue_l${lat}_ti${thi}")
      set(test_args
        "--latency" "${lat}"
        "--ticks-per-output" "${thi}"
        "--total-issued" "1000"
        "--seed" "48763"
      )
      add_simtix_unit_test(${test_name} delay_queue.lua ARGS ${test_args})
    endif()
  endforeach()
endforeach()
