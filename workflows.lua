-- SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
--
-- SPDX-License-Identifier: Apache-2.0

---@alias ConfigValue string|boolean|string[]

---@class Component
---@field paths string[] File globs owned by this component.
---@field depends string[]? Components whose changes can affect this component.

---@alias BuildOptions table<string, ConfigValue>
---@alias BuildProfile BuildOptions Complete, non-additive Xmake options for a build profile.

---@class TestSuite
---@field depends string[] Components whose changes require this suite.
---@field build_options BuildOptions? Arrays use set union, booleans use OR, and scalar values must match.
---@field selectors string[] Native target/case globs; suite membership is defined here, not in test groups.

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
    depends = { "lv.liblv", "lv.bindings.build", "lv.device" },
  }
end

---@type table<SimtixTestSystem, { depends: string[] }>
local simtix_test_systems = {
  opencl = {
    depends = { "formosa-pocl", "tests.opencl" },
  },
  ["kernel-sim"] = {
    depends = { "tests.kernel-sim" },
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

  return {
    depends = depends,
    selectors = test_system == "kernel-sim" and { "kernel-sim-*/simtix." .. sm_model }
      or { "opencl-*/**simtix." .. sm_model },
  }
end

---@param sm_model SimtixSmModel
---@param extra_test_suites string[]?
---@return Workflow
local function make_simtix_sm_workflow(sm_model, extra_test_suites)
  local test_suites = {
    "opencl.simtix." .. sm_model,
    "opencl.validation",
    "kernel-sim.simtix." .. sm_model,
  }
  for _, suite in ipairs(extra_test_suites or {}) do
    test_suites[#test_suites + 1] = suite
  end

  local display_name = (sm_model:gsub("_sm$", " SM"))
  local test_scope = "integration"
  for _, suite in ipairs(test_suites) do
    if suite:match("%.unit$") then
      test_scope = "unit and integration"
      break
    end
  end
  return {
    description = "Validate the simtix " .. display_name .. " with " .. test_scope .. " tests.",
    test_suites = test_suites,
  }
end

---@type WorkflowConfig
local config = {
  components = {
    ["ilha.platform"] = {
      paths = {
        "ilha/xmake.lua",
        "ilha/daemon.lua",
        "ilha/configs/**",
        "ilha/include/**",
        "ilha/lua/**",
        "ilha/platform-data/**",
        "ilha/scripts/**",
        "ilha/src/**",
        "ilha/tests/**",
      },
      depends = { "lv.liblv", "lv.lua", "lv.bindings.build" },
    },
    ["repo.build"] = {
      paths = {
        "xmake.lua",
        "formosa-sdk-xmake.lua",
        "tests/xmake.lua",
        "third-party/xmake.lua",
      },
    },
    ["repo.environment"] = {
      paths = {
        "flake.nix",
        "flake.lock",
        "lv/shell.nix",
      },
    },
    ["lv.build"] = {
      paths = {
        "xmake.lua",
        "lv/xmake.lua",
        "third-party/xmake.lua",
        "xmake/rules/lv/**",
        "xmake/modules/lv/**",
        "xmake/modules/testing.lua",
        "xmake/rules/workflow/**",
        "xmake/rules/riscv/**",
        "tests/xmake.lua",
      },
      depends = { "repo.build", "repo.environment" },
    },
    ["lv.device"] = {
      paths = { "lv/device/**" },
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
        "lv/xmake.lua",
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
    ["lv.bindings.ipc"] = make_lv_binding_component("ipc"),
    ["lv.bindings.nic"] = make_lv_binding_component("nic"),
    ["lv.bindings.simple"] = make_lv_binding_component("simple"),
    ["lv.bindings.systemc"] = make_lv_binding_component("systemc"),
    ["lv.bindings.workload"] = make_lv_binding_component("workload"),
    ["lv.tests.unit"] = {
      paths = { "lv/tests/**" },
      depends = {
        "lv.device",
        "lv.executable",
        "lv.bindings.log",
        "lv.bindings.cp",
        "lv.bindings.dbg",
        "lv.bindings.dma",
        "lv.bindings.dramsys",
        "lv.bindings.ipc",
        "lv.bindings.nic",
        "lv.bindings.simple",
        "lv.bindings.systemc",
        "lv.bindings.workload",
      },
    },
    ["simtix.common"] = {
      paths = {
        "simtix/xmake.lua",
        "simtix/library/simtix.lua",
        "simtix/src/konata/**",
        "simtix/src/tlm_extensions/**",
      },
      depends = { "ilha.platform", "lv.liblv", "repo.build", "repo.environment" },
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
        "ilha.platform",
        "lv.bindings.simple",
      },
    },
    ["simtix.banked_memory"] = {
      paths = { "simtix/lua/banked_memory.lua" },
      depends = { "simtix.mem", "lv.lua", "lv.bindings.nic" },
    },
    ["simtix.banked_cache"] = {
      paths = { "simtix/lua/banked_cache.lua" },
      depends = {
        "simtix.cache",
        "lv.lua",
        "ilha.platform",
        "lv.bindings.nic",
        "lv.bindings.simple",
      },
    },
    ["simtix.pipelined_sm"] = {
      paths = { "simtix/lua/pipelined_sm.lua" },
      depends = {
        "simtix.pipelined_core",
        "simtix.banked_cache",
        "simtix.banked_memory",
        "lv.lua",
        "ilha.platform",
        "lv.bindings.simple",
      },
    },
    ["simtix.tests.build"] = {
      paths = { "simtix/tests/xmake.lua" },
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
    ["simtix.tests.sm"] = {
      paths = { "simtix/tests/sm/**" },
      depends = { "simtix.tests.build" },
    },
    ["tests.cp.common"] = {
      paths = {
        "tests/cp/xmake.lua",
        "tests/cp/build.lua",
        "tests/cp/system.lua",
        "tests/cp/cores.lua",
        "tests/cp/platform.lua",
        "tests/cp/README.md",
        "tests/cp/runtime_test.lua",
        "tests/cp/runtime/**",
        "tests/cp/riscv-test/**",
        "tests/cp/interrupt/**",
        "tests/cp/prog0/**",
        "tests/cp/benchmark/**",
        "tests/cp/coremark/**",
        "tests/cp/dhrystone/**",
        "fw/nanolibc/c/string.c",
        "fw/nanolibc/c/include/**",
        "fw/tinyprintf/**",
        "third-party/riscv-tests",
        "third-party/riscv-tests/**",
      },
      depends = {
        "lv.executable",
        "lv.lua",
        "lv.bindings.dbg",
        "lv.bindings.simple",
        "lv.bindings.systemc",
        "lv.bindings.workload",
      },
    },
    ["tests.simtix.scalar_core"] = {
      paths = {
        "tests/cp/cores/simtix/scalar_core.lua",
      },
      depends = { "tests.cp.common" },
    },
    ["tests.cp.command_processor"] = {
      paths = {
        "tests/cp/cores/cp/command_processor.lua",
      },
      depends = { "tests.cp.common", "lv.bindings.cp" },
    },
    ["tests.plic"] = {
      paths = {
        "tests/plic/**",
      },
      depends = { "tests.cp.command_processor", "lv.device" },
    },
    ["libcomm"] = {
      paths = {
        "libcomm/xmake.lua",
        "libcomm/example/**",
        "libcomm/include/**",
        "libcomm/shell.nix",
      },
      depends = { "repo.build", "repo.environment" },
    },
    ["libcomm.tests"] = {
      paths = { "libcomm/tests/**" },
    },
    ["third-party.perfetto"] = {
      paths = {
        "third-party/perfetto",
        "third-party/perfetto/**",
      },
      depends = { "repo.build" },
    },
    ["fw"] = {
      paths = {
        "fw/xmake.lua",
        "fw/*.c",
        "fw/*.h",
        "fw/freertos/**",
        "fw/nanolibc/**",
        "fw/rom/**",
        "fw/tinyprintf/**",
      },
      depends = { "repo.build", "repo.environment", "lv.device" },
    },
    ["fw.tests"] = {
      paths = { "fw/tests/**" },
    },
    ["hal"] = {
      paths = {
        "hal/xmake.lua",
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
      depends = { "lv.executable", "lv.bindings.workload", "lv.device" },
    },
    ["formosa-llvm"] = {
      paths = { "formosa-llvm/**" },
      depends = { "repo.build", "repo.environment" },
    },
    ["formosa-pocl"] = {
      paths = { "formosa-pocl/**" },
      depends = { "formosa-llvm", "fw", "hal" },
    },
    ["tests.opencl"] = {
      paths = {
        "tests/opencl/**",
      },
      depends = { "formosa-pocl", "ilha.platform" },
    },
    ["formosa-mesa"] = {
      paths = { "formosa-mesa/**" },
      depends = { "formosa-llvm", "formosa-pocl", "hal", "repo.environment" },
    },
    ["tests.vulkan"] = {
      paths = { "tests/vulkan/**", "tests/opencl/run_opencl.lua" },
      depends = { "formosa-mesa", "ilha.platform", "lv.executable" },
    },
    ["tests.kernel-sim"] = {
      paths = { "tests/kernel-sim/**" },
    },
  },
  build_profiles = {
    debug = { mode = "debug" },
    coverage = { mode = "coverage" },
    release = { mode = "release" },
  },
  test_suites = {
    ["vulkan.simtix.atomic_sm"] = {
      depends = { "tests.vulkan", "simtix.atomic_sm" },
      build_options = { ["vulkan-tests"] = true },
      selectors = { "vulkan-compute/*/simtix.atomic_sm" },
    },
    ["vulkan.simtix.pipelined_sm"] = {
      depends = { "tests.vulkan", "simtix.pipelined_sm" },
      build_options = { ["vulkan-tests"] = true },
      selectors = { "vulkan-compute/*/simtix.pipelined_sm" },
    },
    ["opencl.validation"] = {
      depends = { "tests.opencl" },
      selectors = { "opencl-*-validation/validation" },
    },
    ["simtix.sm.integration"] = {
      depends = {
        "simtix.atomic_sm",
        "simtix.pipelined_sm",
        "simtix.tests.sm",
        "lv.executable",
        "lv.lua",
        "lv.bindings.simple",
        "lv.bindings.systemc",
      },
      selectors = { "lv/simtix/sm/**" },
    },
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

      selectors = { "lv/simtix/pipelined_core/**", "simtix-pipelined-abort-test/*" },
    },
    ["simtix.cache.unit"] = {
      depends = {
        "simtix.cache",
        "simtix.banked_cache",
        "simtix.tests.cache",
        "lv.executable",
        "lv.bindings.simple",
        "lv.bindings.systemc",
        "ilha.platform",
      },
      selectors = { "lv/simtix/cache/**", "simtix-cache-*/*" },
    },
    ["simtix.utils.unit"] = {
      depends = {
        "simtix.utils",
        "simtix.tests.utils",
        "lv.executable",
        "lv.bindings.systemc",
      },
      selectors = { "lv/simtix/utils/**" },
    },
    ["simtix.scalar_core"] = {
      depends = {
        "simtix.scalar_core",
        "tests.simtix.scalar_core",
        "lv.executable",
        "lv.bindings.simple",
        "lv.bindings.systemc",
        "lv.bindings.workload",
      },

      selectors = {
        "scalar-riscv-test*/simtix.scalar_core",
        "cp-*/simtix.scalar_core",
        "lv/cp/**",
      },
    },
    ["fw.unit"] = {
      depends = { "fw", "fw.tests" },

      selectors = { "fw-*-test/test" },
    },
    ["hal.unit"] = {
      depends = { "hal", "hal.tests" },
      build_options = { ["install-targets"] = { "formosa-sdk" } },
      selectors = { "hal-lv-tests/*" },
    },
    ["libcomm.unit"] = {
      depends = { "libcomm", "libcomm.tests" },

      selectors = { "libcomm-unit-test/*", "libcomm-install-test/*" },
    },
    ["lv.unit"] = {
      depends = { "lv.tests.unit" },
      build_options = { ["lv-enable-docgen"] = true, ["lv-enable-perfetto"] = true },
      selectors = {
        "lv/unit/**",
        "lv/docgen",
        "lv-log-test/*",
        "lv-statistics-test/*",
        "lv-schema-test/*",
        "lv-address-router-test/*",
        "lv-axi-test/**",
        "lv-axi-rejection-test/**",
      },
    },
    ["ilha.unit"] = {
      depends = { "ilha.platform" },

      selectors = { "lv/ilha/**", "ilha-platform-data-test/*" },
    },
    ["ilha.integration"] = {
      depends = {
        "ilha.platform",
        "simtix.atomic_sm",
        "simtix.pipelined_sm",
        "simtix.pipelined_core",
        "lv.executable",
        "lv.lua",
        "lv.bindings.cp",
        "lv.bindings.dma",
        "lv.bindings.dramsys",
        "lv.bindings.ipc",
        "lv.bindings.simple",
        "lv.bindings.systemc",
        "lv.bindings.workload",
      },
      selectors = { "lv/ilha/integration/**", "ilha-system-test/*" },
    },
    ["lv.bindings"] = {
      depends = {
        "lv.tests.unit",
        "tests.cp.command_processor",
        "tests.plic",
        "tests.ipc",
        "tests.pfreader",
      },
      build_options = { ["lv-enable-perfetto"] = true },
      selectors = {
        "lv/unit/**",
        "lv/docgen",
        "lv-log-test/*",
        "lv-statistics-test/*",
        "lv-schema-test/*",
        "lv-address-router-test/*",
        "lv-axi-test/**",
        "lv-axi-rejection-test/**",
        "lv/cp/**",
        "scalar-riscv-test*/cp.command_processor",
        "cp-*/cp.command_processor",
        "pfreader-test/*",
        "ipc-test/*",
        "ipc-synchronous-test/*",
      },
    },
    ["hal.daemon"] = {
      depends = { "hal", "hal.tests", "tests.hal" },

      selectors = { "hal-daemon-tests/*" },
    },
    ["hal.integration"] = {
      depends = { "hal", "hal.tests", "tests.hal" },
      selectors = { "hal-*/*" },
    },
  },
  workflows = {
    vulkan = {
      description = "Validate Vulkan compute with both Simtix SM models.",
      test_suites = { "vulkan.simtix.atomic_sm", "vulkan.simtix.pipelined_sm" },
    },
    hal = {
      description = "Validate firmware, HAL, libcomm, and HAL daemon behavior.",
      test_suites = { "fw.unit", "hal.unit", "libcomm.unit", "hal.daemon" },
    },
    lv = {
      description = "Validate the complete Lunaverse runtime and bindings.",
      test_suites = { "lv.unit" },
    },
    ["simtix.pipelined_sm"] = make_simtix_sm_workflow(
      "pipelined_sm",
      { "simtix.pipelined_core.unit", "ilha.integration", "simtix.sm.integration" }
    ),
    ["ilha.platform"] = {
      description = "Validate the Ilha platform configuration, bindings, work-group lifecycle, and SM composition.",
      test_suites = { "ilha.unit", "ilha.integration", "simtix.sm.integration" },
    },
    ["simtix.atomic_sm"] = make_simtix_sm_workflow("atomic_sm", { "simtix.sm.integration" }),
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
      description = "Validate the simtix scalar core with the CPU test programs.",
      test_suites = {
        "simtix.scalar_core",
      },
    },
    ["opencl"] = {
      description = "Validate the OpenCL tests and software stack.",
      install = true,
      test_suites = {
        "opencl.simtix.pipelined_sm",
        "opencl.simtix.atomic_sm",
        "opencl.validation",
        "hal.unit",
      },
    },
  },
}

function main() return config end
