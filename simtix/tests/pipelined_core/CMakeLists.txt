# SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
#
# SPDX-License-Identifier: Apache-2.0

set(WARPS_LIST 8 16 32)
set(SUBCORES_LIST 1 2 4)
set(WIDTHS_LIST 1 2 4)
set(IWIS_LIST OFF ON)

add_simtix_unit_test(stack_remap_table stack_remap_table.lua)

foreach(nw ${WARPS_LIST})
  foreach(ns ${SUBCORES_LIST})
    # Ensure num_warps is divisible by num_subcores for valid partitioning
    math(EXPR rem "${nw} % ${ns}")
    if(rem EQUAL 0)
      foreach(wd ${WIDTHS_LIST})
        foreach(iwis ${IWIS_LIST})
          set(test_name "pipelined_frontend_w${nw}_s${ns}_f${wd}")
          set(test_args
            "--num-warps" "${nw}"
            "--num-subcores" "${ns}"
            "--fetch-width" "${wd}"
            "--decode-width" "${wd}"
            "--total-issued" "100"
          )

          if(iwis)
            string(APPEND test_name "_iwis")
            list(APPEND test_args "--enable-iwis")
          endif()

          add_simtix_unit_test(${test_name} frontend.lua ARGS ${test_args})
        endforeach()
      endforeach()
    endif()
  endforeach()
endforeach()

foreach(nw ${WARPS_LIST})
  foreach(ns ${SUBCORES_LIST})
    # Ensure num_warps is divisible by num_subcores for valid partitioning
    math(EXPR rem "${nw} % ${ns}")
    if(rem EQUAL 0)
      set(test_name "pipelined_scoreboard_w${nw}_s${ns}")
      set(test_args
        "--num-warps" "${nw}"
        "--num-subcores" "${ns}"
      )
      add_simtix_unit_test(${test_name} scoreboard.lua ARGS ${test_args})
    endif()
  endforeach()
endforeach()

add_simtix_unit_test(pipelined_ghost_scheduler_ooo ghost_scheduler.lua)

set(LANES_LIST 4 32)
foreach(nw ${WARPS_LIST})
  foreach(ns ${SUBCORES_LIST})
    foreach(nl ${LANES_LIST})
      math(EXPR rem "${nw} % ${ns}")
      if(rem EQUAL 0)
        set(test_name "simple_arbitrator_w${nw}_s${ns}_l${nl}")
        set(test_args
          "--num-warps" "${nw}"
          "--num-subcores" "${ns}"
          "--num-lanes" "${nl}"
          "--total-issued" "100"
          "--arbitrator" "simple"
        )
        add_simtix_unit_test(${test_name} arbitrator.lua ARGS ${test_args})
      endif()
    endforeach()
  endforeach()
endforeach()

set(WARPS_LIST 4 8 16 32)
set(LANES_LIST 4 32)
set(SUBCORES_LIST 1 2 4 8)
set(RCU_LIST 2 4 8)
set(WCU_LIST 1 2)
set(BANKS_LIST 1 2 4)
set(SPORTS_LIST 0 1 2)
set(RPORTS_LIST 1 2 3)
set(WPORTS_LIST 1 2)
set(RF_ARCH_LIST "Baseline" "DuplicateSRF")
foreach(nw ${WARPS_LIST})
  foreach(ns ${SUBCORES_LIST})
    # num_warps % num_subcores = 0
    math(EXPR rem "${nw} % ${ns}")
    if(rem EQUAL 0)

      foreach(nl ${LANES_LIST})

        foreach(rf_arch ${RF_ARCH_LIST})

          foreach(b ${BANKS_LIST})
            # num_regfile_banks * num_subcores <= num_warps
            math(EXPR b_x_ns "${b} * ${ns}")
            if(b_x_ns LESS_EQUAL nw)

              foreach(rcu ${RCU_LIST})
                # num_read_collect_units <= num_regfile_banks
                if(rcu LESS_EQUAL b)

                  foreach(wcu ${WCU_LIST})
                    # num_write_collect_units <= num_regfile_banks
                    if(wcu LESS_EQUAL b)

                      foreach(sport ${SPORTS_LIST})

                        if(sport EQUAL 0)
                          foreach(rport ${RPORTS_LIST})
                            foreach(wport ${WPORTS_LIST})

                              set(test_name "pipelined_arbitrator_w${nw}_s${ns}_l${nl}_rcu${rcu}_wcu${wcu}_b${b}_sp${sport}_rp${rport}_wp${wport}_${rf_arch}")

                              set(test_args
                                "--num-warps" "${nw}"
                                "--num-subcores" "${ns}"
                                "--num-lanes" "${nl}"
                                "--num-read-collect-units" "${rcu}"
                                "--num-write-collect-units" "${wcu}"
                                "--num-regfile-banks" "${b}"
                                "--num-shared-ports" "${sport}"
                                "--num-read-ports" "${rport}"
                                "--num-write-ports" "${wport}"
                                "--total-issued" "100"
                                "--arbitrator" "pipelined"
                                "--arbitrator-config" "${rf_arch}"
                              )

                              add_simtix_unit_test(${test_name} arbitrator.lua ARGS ${test_args})

                            endforeach()
                          endforeach()

                        else()
                          set(rport 0)
                          set(wport 0)

                          set(test_name "pipelined_arbitrator_w${nw}_s${ns}_l${nl}_rcu${rcu}_wcu${wcu}_b${b}_sp${sport}_rp${rport}_wp${wport}_${rf_arch}")

                          set(test_args
                            "--num-warps" "${nw}"
                            "--num-subcores" "${ns}"
                            "--num-lanes" "${nl}"
                            "--num-read-collect-units" "${rcu}"
                            "--num-write-collect-units" "${wcu}"
                            "--num-regfile-banks" "${b}"
                            "--num-shared-ports" "${sport}"
                            "--num-read-ports" "${rport}"
                            "--num-write-ports" "${wport}"
                            "--total-issued" "100"
                            "--arbitrator" "pipelined"
                            "--arbitrator-config" "${rf_arch}"
                          )

                          add_simtix_unit_test(${test_name} arbitrator.lua ARGS ${test_args})

                        endif()

                      endforeach() # end sport

                    endif()
                  endforeach() # end wcu

                endif()
              endforeach() # end rcu

            endif()
          endforeach() # end b
        endforeach()

      endforeach() # end nl

    endif()
  endforeach() # end ns
endforeach() # end nw

set(LSU_LIST "simple" "coalescing")
set(LSU_LANES_LIST 4 8 32 64)
foreach(lsu ${LSU_LIST})
  foreach(nl ${LSU_LANES_LIST})
    set(test_name "pipelined_${lsu}_lsu_l${nl}")
    set(test_args
      "--lsu" "${lsu}"
      "--num-lanes" "${nl}"
      "--total-iter" "1000"
    )
    add_simtix_unit_test(${test_name} lsu.lua ARGS ${test_args})
  endforeach()
endforeach()

add_simtix_unit_test(pipelined_coalescing_lsu_deterministic lsu_coalescing.lua)
