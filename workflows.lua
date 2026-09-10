-- SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
--
-- SPDX-License-Identifier: Apache-2.0

---@alias ConfigValue string|boolean|string[]

---@class Component
---@field paths string[] File globs owned by this component.
---@field depends string[]? Components whose changes can affect this component.

---@alias BuildAdapterOptions table<string, ConfigValue>
---@alias BuildProfile table<string, BuildAdapterOptions> Complete, non-additive options keyed by adapter, such as "cmake" or "xmake".

---@class TestAdapter
---@field build_options table<string, ConfigValue>? Options merged by value type: string arrays use set union, booleans use OR, and scalar values must match.
---@field selectors string[] Test selectors interpreted and unioned by this adapter.

---@class TestSuite
---@field depends string[] Components whose changes require this suite.
---@field adapters table<string, TestAdapter> Test definitions keyed by adapter name, such as "cmake" or "xmake".

---@class Workflow
---@field description string Human-readable integration workflow.
---@field test_suites string[] Test suites requested by this workflow.
---@field install boolean? Build and install this workflow before tests are run explicitly.

---@class WorkflowConfig
---@field components table<string, Component>
---@field build_profiles table<string, BuildProfile>
---@field test_suites table<string, TestSuite>
---@field workflows table<string, Workflow>

---@alias SimtixSmModel "pipelined_sm"|"atomic_sm"
---@alias SimtixTestSystem "opencl"|"kernel-sim"

---@param name string
---@return Component
local function make_lv_binding_component(name)
  return {
    paths = { "lv/src/lv/bindings/" .. name .. "/**" },
    depends = { "lv.liblv", "lv.bindings.build" },
  }
end

---@type table<SimtixTestSystem, { depends: string[], enabled_tests: string[] }>
local simtix_test_systems = {
  opencl = {
    depends = { "formosa-pocl", "tests.opencl" },
    enabled_tests = { "formosa", "opencl" },
  },
  ["kernel-sim"] = {
    depends = { "tests.kernel-sim" },
    enabled_tests = { "kernel-sim" },
  },
}

---@param test_system SimtixTestSystem
---@param sm_model SimtixSmModel
---@return TestSuite
local function make_simtix_sm_test_suite(test_system, sm_model)
  local system = assert(simtix_test_systems[test_system])
  local depends = { "simtix." .. sm_model }
  for _, component in ipairs(system.depends) do
    depends[#depends + 1] = component
  end
  local enabled_tests = {}
  for _, test in ipairs(system.enabled_tests) do
    enabled_tests[#enabled_tests + 1] = test
  end

  return {
    depends = depends,
    adapters = {
      cmake = {
        build_options = {
          ENABLE_PROJECTS = { "simtix", "tests" },
          TESTS_ENABLE_TESTS = enabled_tests,
        },
        selectors = {
          test_system .. "\\..*\\.simtix\\." .. sm_model,
        },
      },
    },
  }
end

---@param sm_model SimtixSmModel
---@param extra_test_suites string[]?
---@return Workflow
local function make_simtix_sm_workflow(sm_model, extra_test_suites)
  local test_suites = {
    "opencl.simtix." .. sm_model,
    "kernel-sim.simtix." .. sm_model,
  }
  for _, suite in ipairs(extra_test_suites or {}) do
    test_suites[#test_suites + 1] = suite
  end

  local display_name = (sm_model:gsub("_sm$", " SM"))
  local test_scope = #test_suites > 2 and "unit and integration" or "integration"
  return {
    description = "Validate the simtix " .. display_name .. " with " .. test_scope .. " tests.",
    test_suites = test_suites,
  }
end

---@type WorkflowConfig
local config = {
  components = {
    ["repo.cmake"] = {
      paths = {
        "CMakeLists.txt",
        "tests/CMakeLists.txt",
        "cmake/**",
        "third-party/*.cmake",
      },
    },
    ["repo.environment"] = {
      paths = {
        "flake.nix",
        "flake.lock",
      },
    },
    ["lv.build"] = {
      paths = {
        "lv/CMakeLists.txt",
        "lv/cmake/**",
      },
      depends = { "repo.cmake", "repo.environment" },
    },
    ["lv.liblv"] = {
      paths = {
        "lv/include/liblv/**",
        "lv/src/liblv/**",
      },
      depends = { "lv.build", "libcomm", "third-party.perfetto" },
    },
    ["lv.bindings.build"] = {
      paths = {
        "lv/src/lv/CMakeLists.txt",
      },
      depends = { "lv.build" },
    },
    ["lv.executable"] = {
      paths = {
        "lv/src/lv/main.cc",
      },
      depends = { "lv.liblv", "lv.bindings.build" },
    },
    ["lv.lua"] = {
      paths = {
        "lv/lua/sc_module.lua",
        "lv/lua/util.lua",
      },
      depends = { "lv.liblv" },
    },
    ["lv.bindings.log"] = {
      paths = { "lv/src/lv/bindings/log.cc" },
      depends = { "lv.liblv", "lv.bindings.build" },
    },
    ["lv.bindings.cp"] = make_lv_binding_component("cp"),
    ["lv.bindings.dbg"] = make_lv_binding_component("dbg"),
    ["lv.bindings.dma"] = make_lv_binding_component("dma"),
    ["lv.bindings.dramsys"] = make_lv_binding_component("dramsys"),
    ["lv.bindings.formosa"] = {
      paths = {
        "lv/src/lv/bindings/formosa/**",
        "lv/lua/transactor_sm.lua",
      },
      depends = { "lv.liblv", "lv.lua", "lv.bindings.build" },
    },
    ["lv.bindings.ipc"] = make_lv_binding_component("ipc"),
    ["lv.bindings.nic"] = make_lv_binding_component("nic"),
    ["lv.bindings.simple"] = make_lv_binding_component("simple"),
    ["lv.bindings.systemc"] = make_lv_binding_component("systemc"),
    ["lv.bindings.workload"] = make_lv_binding_component("workload"),
    ["lv.tests.unit"] = {
      paths = { "lv/tests/**" },
      depends = {
        "lv.executable",
        "lv.bindings.log",
        "lv.bindings.cp",
        "lv.bindings.dbg",
        "lv.bindings.dma",
        "lv.bindings.dramsys",
        "lv.bindings.formosa",
        "lv.bindings.ipc",
        "lv.bindings.nic",
        "lv.bindings.simple",
        "lv.bindings.systemc",
        "lv.bindings.workload",
      },
    },
    ["simtix.common"] = {
      paths = {
        "simtix/CMakeLists.txt",
        "simtix/library/simtix.lua",
        "simtix/src/konata/**",
        "simtix/src/tlm_extensions/**",
        "simtix/src/CMakeLists.txt",
      },
      depends = { "lv.liblv", "repo.cmake", "repo.environment" },
    },
    ["simtix.utils"] = {
      paths = { "simtix/src/utils/**" },
      depends = { "simtix.common" },
    },
    ["simtix.cache"] = {
      paths = { "simtix/src/cache/**" },
      depends = { "simtix.common", "simtix.utils" },
    },
    ["simtix.core_base"] = {
      paths = {
        "simtix/src/cores/*.h",
        "simtix/src/cores/*.cc",
        "simtix/src/cores/sched/**",
      },
      depends = { "simtix.common" },
    },
    ["simtix.atomic_core"] = {
      paths = { "simtix/src/cores/atomic/**" },
      depends = { "simtix.core_base" },
    },
    ["simtix.pipelined_core"] = {
      paths = { "simtix/src/cores/pipelined/**" },
      depends = { "simtix.core_base", "simtix.utils" },
    },
    ["simtix.scalar_core"] = {
      paths = { "simtix/src/cores/scalar/**" },
      depends = { "simtix.core_base" },
    },
    ["simtix.mem"] = {
      paths = { "simtix/src/mem/**" },
      depends = { "simtix.common" },
    },
    ["simtix.atomic_sm"] = {
      paths = { "simtix/lua/atomic_sm.lua" },
      depends = {
        "simtix.atomic_core",
        "simtix.cache",
        "simtix.mem",
        "lv.lua",
        "lv.bindings.formosa",
        "lv.bindings.simple",
      },
    },
    ["simtix.banked_memory"] = {
      paths = { "simtix/lua/banked_memory.lua" },
      depends = { "simtix.mem", "lv.lua", "lv.bindings.nic" },
    },
    ["simtix.pipelined_sm"] = {
      paths = { "simtix/lua/pipelined_sm.lua" },
      depends = {
        "simtix.pipelined_core",
        "simtix.cache",
        "simtix.banked_memory",
        "lv.lua",
        "lv.bindings.formosa",
        "lv.bindings.simple",
      },
    },
    ["simtix.tests.build"] = {
      paths = { "simtix/tests/CMakeLists.txt" },
    },
    ["simtix.tests.pipelined_core"] = {
      paths = { "simtix/tests/pipelined_core/**" },
      depends = { "simtix.tests.build" },
    },
    ["simtix.tests.cache"] = {
      paths = { "simtix/tests/cache/**" },
      depends = { "simtix.tests.build" },
    },
    ["simtix.tests.utils"] = {
      paths = { "simtix/tests/utils/**" },
      depends = { "simtix.tests.build" },
    },
    ["tests.simtix.scalar_core"] = {
      paths = {
        "tests/cp/CMakeLists.txt",
        "tests/cp/scalar_core.lua",
        "tests/cp/riscv-test/**",
      },
    },
    ["tests.cp"] = {
      paths = {
        "tests/cp/CMakeLists.txt",
        "tests/cp/cp.lua",
        "tests/cp/riscv-test/**",
      },
      depends = { "lv.executable", "lv.bindings.workload" },
    },
    ["libcomm"] = {
      paths = {
        "libcomm/CMakeLists.txt",
        "libcomm/example/**",
        "libcomm/include/**",
        "libcomm/shell.nix",
      },
      depends = { "repo.cmake", "repo.environment" },
    },
    ["libcomm.tests"] = {
      paths = { "libcomm/tests/**" },
    },
    ["third-party.perfetto"] = {
      paths = {
        "third-party/perfetto",
        "third-party/perfetto/**",
        "third-party/perfetto.cmake",
      },
      depends = { "repo.cmake" },
    },
    ["fw"] = {
      paths = {
        "fw/CMakeLists.txt",
        "fw/*.c",
        "fw/*.h",
        "fw/freertos/**",
        "fw/nanolibc/**",
        "fw/rom/**",
        "fw/tinyprintf/**",
      },
      depends = { "repo.cmake", "repo.environment" },
    },
    ["fw.tests"] = {
      paths = { "fw/tests/**" },
    },
    ["hal"] = {
      paths = {
        "hal/CMakeLists.txt",
        "hal/cmake/**",
        "hal/default.nix",
        "hal/include/**",
        "hal/src/**",
      },
      depends = { "fw", "libcomm" },
    },
    ["hal.tests"] = {
      paths = { "hal/tests/**" },
    },
    ["tests.hal"] = {
      paths = { "tests/hal/**" },
      depends = { "hal", "hal.tests" },
    },
    ["tests.ipc"] = {
      paths = { "tests/ipc/**" },
      depends = { "lv.liblv", "libcomm" },
    },
    ["tests.pfreader"] = {
      paths = { "tests/pfreader/**" },
      depends = { "lv.executable", "lv.bindings.workload" },
    },
    ["formosa-llvm"] = {
      paths = { "formosa-llvm/**" },
      depends = { "repo.cmake", "repo.environment" },
    },
    ["formosa-pocl"] = {
      paths = { "formosa-pocl/**" },
      depends = { "formosa-llvm", "fw", "hal" },
    },
    ["tests.opencl"] = {
      paths = {
        "tests/formosa/**",
        "tests/opencl/**",
      },
      depends = { "formosa-pocl" },
    },
    ["tests.kernel-sim"] = {
      paths = { "tests/kernel-sim/**" },
    },
  },
  build_profiles = {
    debug = {
      cmake = {
        CMAKE_BUILD_TYPE = "Debug",
        ENABLE_COVERAGE = false,
      },
    },
    coverage = {
      cmake = {
        CMAKE_BUILD_TYPE = "Debug",
        ENABLE_COVERAGE = true,
      },
    },
    release = {
      cmake = {
        CMAKE_BUILD_TYPE = "Release",
        ENABLE_COVERAGE = false,
      },
    },
  },
  test_suites = {
    ["opencl.simtix.pipelined_sm"] = make_simtix_sm_test_suite("opencl", "pipelined_sm"),
    ["opencl.simtix.atomic_sm"] = make_simtix_sm_test_suite("opencl", "atomic_sm"),
    ["kernel-sim.simtix.pipelined_sm"] = make_simtix_sm_test_suite("kernel-sim", "pipelined_sm"),
    ["kernel-sim.simtix.atomic_sm"] = make_simtix_sm_test_suite("kernel-sim", "atomic_sm"),
    ["simtix.pipelined_core.unit"] = {
      depends = {
        "simtix.pipelined_core",
        "simtix.tests.pipelined_core",
        "lv.executable",
        "lv.lua",
        "lv.bindings.simple",
        "lv.bindings.systemc",
      },
      adapters = {
        cmake = {
          build_options = {
            ENABLE_PROJECTS = { "simtix" },
            SIMTIX_ENABLE_TESTING = true,
          },
          selectors = {
            "simtix\\.stack_remap_table",
            "simtix\\.pipelined_frontend",
            "simtix\\.pipelined_scoreboard",
            "simtix\\.pipelined_ghost_scheduler_ooo",
            "simtix\\.simple_arbitrator",
            "simtix\\.pipelined_arbitrator",
            "simtix\\.pipelined_.*_lsu",
            "simtix\\.pipelined_coalescing_lsu_deterministic",
          },
        },
      },
    },
    ["simtix.cache.unit"] = {
      depends = {
        "simtix.cache",
        "simtix.tests.cache",
        "lv.executable",
        "lv.bindings.simple",
        "lv.bindings.systemc",
      },
      adapters = {
        cmake = {
          build_options = {
            ENABLE_PROJECTS = { "simtix" },
            SIMTIX_ENABLE_TESTING = true,
          },
          selectors = { "simtix\\.cache(_|\\.).*" },
        },
      },
    },
    ["simtix.utils.unit"] = {
      depends = {
        "simtix.utils",
        "simtix.tests.utils",
        "lv.executable",
        "lv.bindings.systemc",
      },
      adapters = {
        cmake = {
          build_options = {
            ENABLE_PROJECTS = { "simtix" },
            SIMTIX_ENABLE_TESTING = true,
          },
          selectors = { "simtix\\.delay_queue.*" },
        },
      },
    },
    ["simtix.scalar_core.riscv-test"] = {
      depends = {
        "simtix.scalar_core",
        "tests.simtix.scalar_core",
        "lv.executable",
        "lv.bindings.simple",
        "lv.bindings.systemc",
        "lv.bindings.workload",
      },
      adapters = {
        cmake = {
          build_options = {
            ENABLE_PROJECTS = { "simtix", "tests" },
            TESTS_ENABLE_TESTS = { "cp" },
          },
          selectors = { "scalar_core_riscv_test" },
        },
      },
    },
    ["fw.unit"] = {
      depends = { "fw", "fw.tests" },
      adapters = {
        cmake = {
          build_options = {
            ENABLE_PROJECTS = { "fw" },
            FW_ENABLE_TESTING = true,
          },
          selectors = { "fw" },
        },
      },
    },
    ["hal.unit"] = {
      depends = { "hal", "hal.tests" },
      adapters = {
        cmake = {
          build_options = {
            CMAKE_INSTALL_PREFIX = "outputs/out",
            ENABLE_PROJECTS = { "hal", "fw", "libcomm" },
            HAL_ENABLE_TESTING = true,
          },
          selectors = { "hal\\.lv_.*" },
        },
      },
    },
    ["libcomm.unit"] = {
      depends = { "libcomm", "libcomm.tests" },
      adapters = {
        cmake = {
          build_options = {
            ENABLE_PROJECTS = { "libcomm" },
            LIBCOMM_ENABLE_TESTING = true,
          },
          selectors = { "libcomm\\.unit" },
        },
      },
    },
    ["lv.unit"] = {
      depends = { "lv.tests.unit" },
      adapters = {
        cmake = {
          build_options = {
            ENABLE_PROJECTS = { "lv" },
            LV_ENABLE_DOCGEN = true,
            LV_ENABLE_PERFETTO = true,
            LV_ENABLE_TESTING = true,
          },
          selectors = { "lv\\.unit\\..*" },
        },
      },
    },
    ["lv.bindings"] = {
      depends = {
        "lv.tests.unit",
        "tests.cp",
        "tests.ipc",
        "tests.pfreader",
      },
      adapters = {
        cmake = {
          build_options = {
            ENABLE_PROJECTS = { "lv", "tests" },
            LV_ENABLE_PERFETTO = true,
            LV_ENABLE_TESTING = true,
            TESTS_ENABLE_TESTS = { "cp", "ipc", "pfreader" },
          },
          selectors = {
            ".*/.*\\.lua",
            "lv.unit.log",
            "cp",
            "pfreader",
            "ipc_test",
          },
        },
      },
    },
    ["hal.integration"] = {
      depends = { "hal", "hal.tests", "tests.hal" },
      adapters = {
        cmake = {
          build_options = {
            ENABLE_PROJECTS = { "hal", "lv", "simtix", "tests" },
            HAL_ENABLE_TESTING = true,
            TESTS_ENABLE_TESTS = { "formosa", "hal" },
          },
          selectors = { "hal", "opencl" },
        },
      },
    },
  },
  workflows = {
    ["simtix.pipelined_sm"] = make_simtix_sm_workflow(
      "pipelined_sm",
      { "simtix.pipelined_core.unit" }
    ),
    ["simtix.atomic_sm"] = make_simtix_sm_workflow("atomic_sm"),
    ["simtix.cache"] = {
      description = "Validate the simtix cache model with unit and integration tests.",
      test_suites = {
        "simtix.cache.unit",
        "opencl.simtix.atomic_sm",
        "opencl.simtix.pipelined_sm",
        "kernel-sim.simtix.atomic_sm",
        "kernel-sim.simtix.pipelined_sm",
      },
    },
    ["simtix.scalar_core"] = {
      description = "Validate the simtix scalar core with the RV64 riscv-test workload.",
      test_suites = {
        "simtix.scalar_core.riscv-test",
      },
    },
    ["opencl"] = {
      description = "Validate the OpenCL tests and software stack.",
      install = true,
      test_suites = {
        "opencl.simtix.pipelined_sm",
        "opencl.simtix.atomic_sm",
        "kernel-sim.simtix.pipelined_sm",
        "kernel-sim.simtix.atomic_sm",
        "hal.unit",
      },
    },
  },
}

function main() return config end
