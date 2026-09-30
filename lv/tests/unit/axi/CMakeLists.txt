# SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
#
# SPDX-License-Identifier: Apache-2.0

set(TARGET lv-unit-axi-adapters)

add_executable(${TARGET}
  axi_adapters_test.cc
  axi_safety_test.cc
  axi_reset_test.cc
  ${formosa_SOURCE_DIR}/lv/src/lv/bindings/simple/memory.cc
)

target_compile_features(${TARGET}
  PRIVATE cxx_std_17
)

target_include_directories(${TARGET}
  PRIVATE "${formosa_SOURCE_DIR}"
)

target_link_libraries(${TARGET}
  PRIVATE liblv systemc elfio::elfio
)

add_test(NAME lv.unit.axi.adapters COMMAND $<TARGET_FILE:${TARGET}>)
add_test(NAME lv.unit.axi.write-aw-after-w
  COMMAND $<TARGET_FILE:${TARGET}> --axi-write-aw-after-w)
add_test(NAME lv.unit.axi.target-reset.read
  COMMAND $<TARGET_FILE:${TARGET}> --axi-target-reset read)
add_test(NAME lv.unit.axi.target-reset.write
  COMMAND $<TARGET_FILE:${TARGET}> --axi-target-reset write)
add_test(NAME lv.unit.axi.initiator-reset
  COMMAND $<TARGET_FILE:${TARGET}> --axi-initiator-reset)

foreach(case reset no-reset)
  add_test(NAME lv.unit.axi.write-response-gap.${case}
    COMMAND $<TARGET_FILE:${TARGET}> --axi-write-response-gap ${case})
  set_tests_properties(lv.unit.axi.write-response-gap.${case}
    PROPERTIES ENVIRONMENT "LV_AXI_WRITE_RESP_GAP=8")
endforeach()

foreach(old_op read write)
  foreach(new_op read write)
    foreach(channel same-channel cross-channel)
      set(test_name lv.unit.axi.reset.serialization.old-${old_op}.new-${new_op}.${channel})
      add_test(NAME ${test_name}
        COMMAND $<TARGET_FILE:${TARGET}> --axi-reset-serialization ${old_op} ${new_op} ${channel})
      set_tests_properties(${test_name} PROPERTIES ENVIRONMENT
        "LV_AXI_WRITE_RESP_GAP=0;LV_AXI_READ_BEAT_GAP=0;LV_AXI_READ_BURST_GAP=0")
    endforeach()
  endforeach()
endforeach()

foreach(split 0 1)
  if(split)
    set(split_name split)
  else()
    set(split_name no-split)
  endif()
  foreach(read 0 1)
    foreach(case plain9 plain3 short long zero plain-zero null size huge-burst cross
                 later-cross address-overflow address-width byte-zero byte1 byte2 byte3
                 plain-valid null-enables)
      add_test(NAME lv.unit.axi.safety.${case}.${split}.${read}
        COMMAND $<TARGET_FILE:${TARGET}> --axi-safety ${case} ${split} ${read} 2)
    endforeach()
    if(read)
      set(operation read)
    else()
      set(operation write)
    endif()
    foreach(case streaming-zero streaming-short streaming-equal streaming-long
                 unaligned aligned-narrow wrap-target)
      add_test(NAME lv.unit.axi.safety.${case}.${split_name}.${operation}
        COMMAND $<TARGET_FILE:${TARGET}> --axi-safety ${case} ${split} ${read} 2)
    endforeach()
    if(NOT split)
      add_test(NAME lv.unit.axi.wrap.initiator.${operation}
        COMMAND ${CMAKE_COMMAND}
          -DPROGRAM=$<TARGET_FILE:${TARGET}> -DCASE=wrap-initiator
          -DSPLIT=0 -DREAD=${read} -DBEATS=4
          "-DEXPECTED_MESSAGE=AXI->TLM ${operation}: WRAP AXI bursts are unsupported"
          -P ${CMAKE_CURRENT_SOURCE_DIR}/expect_axi_fatal.cmake)
      add_test(NAME lv.unit.axi.unaligned.initiator.${operation}
        COMMAND ${CMAKE_COMMAND}
          -DPROGRAM=$<TARGET_FILE:${TARGET}> -DCASE=unaligned-initiator
          -DSPLIT=0 -DREAD=${read} -DBEATS=2
          "-DEXPECTED_MESSAGE=AXI->TLM ${operation}: unaligned AXI beats are unsupported"
          -P ${CMAKE_CURRENT_SOURCE_DIR}/expect_axi_fatal.cmake)
    endif()
    foreach(metadata plain extension)
      foreach(case max-beats too-many-beats page-end page-cross)
        add_test(NAME lv.unit.axi.geometry.${case}.${metadata}.${split_name}.${operation}
          COMMAND $<TARGET_FILE:${TARGET}> --axi-safety geometry-${metadata}-${case} ${split} ${read} 2)
      endforeach()
    endforeach()
    foreach(side target initiator)
      if(side STREQUAL "initiator" AND split)
        continue()
      endif()
      if(side STREQUAL "target")
        set(direction "TLM->AXI")
      else()
        set(direction "AXI->TLM")
      endif()
      foreach(beats 1 2)
        set(beats_name ${beats}-beat)
        add_test(NAME lv.unit.axi.fixed.${side}.${split_name}.${operation}.${beats_name}
          COMMAND ${CMAKE_COMMAND}
            -DPROGRAM=$<TARGET_FILE:${TARGET}> -DCASE=fixed-${side}
            -DSPLIT=${split} -DREAD=${read} -DBEATS=${beats}
            "-DEXPECTED_MESSAGE=${direction} ${operation}: FIXED AXI bursts are unsupported"
            -P ${CMAKE_CURRENT_SOURCE_DIR}/expect_axi_fatal.cmake)
      endforeach()
    endforeach()
  endforeach()
endforeach()

# Independent pin stimulus. A fatal must match both exit status and diagnostic.
foreach(read 0 1)
  if(read)
    set(operation read)
  else()
    set(operation write)
  endif()
  foreach(case oversized raw-size len raw-len page range early-wlast missing-wlast)
    if(read AND case MATCHES "wlast")
      continue()
    endif()
    if(case MATCHES "size")
      set(reason "AxSIZE exceeds data bus width")
    elseif(case MATCHES "len")
      set(reason "AxLEN exceeds 255")
    elseif(case STREQUAL "page")
      set(reason "burst crosses 4-KiB boundary")
    elseif(case STREQUAL "range")
      set(reason "burst exceeds address range")
    else()
      set(reason "WLAST does not match AWLEN")
    endif()
    add_test(NAME lv.unit.axi.protocol.${case}.${operation}
      COMMAND ${CMAKE_COMMAND}
        -DPROGRAM=$<TARGET_FILE:${TARGET}> -DCASE=${case}-initiator
        -DSPLIT=0 -DREAD=${read} -DBEATS=2
        "-DEXPECTED_MESSAGE=AXI->TLM ${operation}: ${reason}"
        -P ${CMAKE_CURRENT_SOURCE_DIR}/expect_axi_fatal.cmake)
  endforeach()
endforeach()
foreach(case wrong-rid late-wrong-rid wrong-bid early-rlast missing-rlast)
  if(case STREQUAL "wrong-bid")
    set(reason "write: BID does not match AWID")
  elseif(case MATCHES "wrong-rid")
    set(reason "read: RID does not match ARID")
  else()
    set(reason "read: RLAST does not match ARLEN")
  endif()
  add_test(NAME lv.unit.axi.protocol.${case}
    COMMAND ${CMAKE_COMMAND}
      -DPROGRAM=$<TARGET_FILE:${TARGET}> -DCASE=${case}-response
      -DSPLIT=0 -DREAD=0 -DBEATS=2
      "-DEXPECTED_MESSAGE=TLM->AXI ${reason}"
      -P ${CMAKE_CURRENT_SOURCE_DIR}/expect_axi_fatal.cmake)
endforeach()
foreach(case absent-bid valid-read)
  add_test(NAME lv.unit.axi.protocol.${case}
    COMMAND $<TARGET_FILE:${TARGET}> --axi-safety ${case}-response 0 0 2)
endforeach()

add_test(NAME lv.unit.axi.protocol.missing-wlast.single-beat
  COMMAND ${CMAKE_COMMAND}
    -DPROGRAM=$<TARGET_FILE:${TARGET}> -DCASE=missing-wlast-initiator
    -DSPLIT=0 -DREAD=0 -DBEATS=1
    "-DEXPECTED_MESSAGE=AXI->TLM write: WLAST does not match AWLEN"
    -P ${CMAKE_CURRENT_SOURCE_DIR}/expect_axi_fatal.cmake)
set_tests_properties(lv.unit.axi.adapters PROPERTIES ENVIRONMENT
  "LV_AXI_WRITE_RESP_GAP=0;LV_AXI_READ_BEAT_GAP=0;LV_AXI_READ_BURST_GAP=0")
