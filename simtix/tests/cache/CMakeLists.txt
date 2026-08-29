# SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
#
# SPDX-License-Identifier: Apache-2.0

include(Catch)

function(add_simtix_cache_cpp_test NAME)
  set(TARGET simtix_cache_${NAME}_test)
  set(TEST_LABELS "simtix;unit;cache")

  # Catch2 forwards PROPERTIES through its generated discovery script. Preserve
  # the label list across both CMake parsing layers.
  string(REPLACE ";" "\\\\\\\\\\\\\\\\;" TEST_LABELS "${TEST_LABELS}")

  add_executable(${TARGET}
    ${ARGN}
  )

  target_include_directories(${TARGET}
    PRIVATE ${PROJECT_SOURCE_DIR}/src
  )

  target_link_libraries(${TARGET}
    PRIVATE Catch2::Catch2
    PRIVATE simtix
  )

  catch_discover_tests(${TARGET}
    TEST_PREFIX "simtix.cache.${NAME}."
    PROPERTIES LABELS "${TEST_LABELS}"
  )
endfunction()

function(add_simtix_cache_systemc_test NAME)
  set(TARGET simtix_cache_${NAME}_test)

  add_executable(${TARGET}
    ${ARGN}
  )

  target_include_directories(${TARGET}
    PRIVATE ${PROJECT_SOURCE_DIR}/src
  )

  target_link_libraries(${TARGET}
    PRIVATE simtix
  )

  add_test(
    NAME simtix.cache.${NAME}
    COMMAND ${TARGET}
  )
  set_tests_properties(simtix.cache.${NAME}
    PROPERTIES LABELS "simtix;unit;cache"
  )
endfunction()

function(add_simtix_cache_lua_test NAME SCRIPT)
  add_lv_test_script(simtix.${NAME} ${SCRIPT} ${ARGN}
    LABELS "simtix" "unit" "cache"
  )
endfunction()

add_simtix_cache_cpp_test(tag_array tag_array_test.cc)
add_simtix_cache_cpp_test(data_array data_array_test.cc)
add_simtix_cache_cpp_test(mem_payload mem_payload_test.cc)
add_simtix_cache_cpp_test(param param_test.cc)
add_simtix_cache_cpp_test(mshr mshr_file_test.cc)
add_simtix_cache_systemc_test(mshr_file_module mshr_file_module_test.cc)
add_simtix_cache_systemc_test(write_buffer_module write_buffer_module_test.cc)
add_simtix_cache_systemc_test(victim_buffer_module victim_buffer_module_test.cc)
add_simtix_cache_systemc_test(cache_module_basic cache_module_basic_test.cc)
add_simtix_cache_systemc_test(cache_module_memory_response
  cache_module_memory_response_test.cc)
add_simtix_cache_systemc_test(cache_module_hazard cache_module_hazard_test.cc)
add_simtix_cache_systemc_test(cache_module_atomic cache_module_atomic_test.cc)
add_simtix_cache_systemc_test(cache_module_mmio cache_module_mmio_test.cc)
add_simtix_cache_systemc_test(cache_module_watchdog cache_module_watchdog_test.cc)

add_custom_target(simtix_cache_cache_module_test
  DEPENDS
    simtix_cache_cache_module_basic_test
    simtix_cache_cache_module_memory_response_test
    simtix_cache_cache_module_hazard_test
    simtix_cache_cache_module_atomic_test
    simtix_cache_cache_module_mmio_test
    simtix_cache_cache_module_watchdog_test
)

option(SIMTIX_ENABLE_LEGACY_CACHE_LUA_TESTS
  "Register the legacy Lua cache integration tests"
  OFF
)

# Define the choices for each option
set(REPLACEMENT_POLICIES_ALL "fifo" "lru" "random")
set(REPLACEMENT_POLICIES_NO_RANDOM "fifo" "lru")

set(WRITE_HIT_POLICIES "WriteBack" "WriteThrough")
set(WRITE_MISS_POLICIES "WriteAllocate" "WriteNoAllocate")

add_simtix_cache_lua_test(cache_read cache_read.lua)
add_simtix_cache_lua_test(cache_non_cacheable cache_non_cacheable.lua)
add_simtix_cache_lua_test(cache_flush cache_flush.lua)
add_simtix_cache_lua_test(cache_invalidate cache_invalidate.lua)

# cache_write: only fifo/lru (exclude random)
foreach(REPL IN LISTS REPLACEMENT_POLICIES_NO_RANDOM)
  foreach(HIT IN LISTS WRITE_HIT_POLICIES)
    foreach(MISS IN LISTS WRITE_MISS_POLICIES)
      set(SUFFIX "${REPL}.${HIT}.${MISS}")

      add_simtix_cache_lua_test(cache_write.${SUFFIX} cache_write.lua
        ARGS
          --replacement_policy "${REPL}"
          --write_hit_policy "${HIT}"
          --write_miss_policy "${MISS}"
      )
    endforeach()
  endforeach()
endforeach()

# cache_random is model-based and deterministic for a given seed. Cover random
# replacement here because cache_write intentionally avoids policy-dependent
# victim assertions for that policy.
foreach(REPL IN LISTS REPLACEMENT_POLICIES_ALL)
  foreach(HIT IN LISTS WRITE_HIT_POLICIES)
    foreach(MISS IN LISTS WRITE_MISS_POLICIES)
      set(SUFFIX "${REPL}.${HIT}.${MISS}")

      add_simtix_cache_lua_test(cache_random.${SUFFIX} cache_random.lua
        ARGS
          --replacement_policy "${REPL}"
          --write_hit_policy "${HIT}"
          --write_miss_policy "${MISS}"
      )
    endforeach()
  endforeach()
endforeach()

if(SIMTIX_ENABLE_LEGACY_CACHE_LUA_TESTS)
  add_simtix_unit_test(cache_prefetch cache_prefetch.lua)
  add_simtix_unit_test(cache_pc_prefetch cache_pc_prefetch.lua)
endif()
