-- SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
--
-- SPDX-License-Identifier: Apache-2.0

target("lv", function()
  local function case(name, script, args)
    add_tests("simtix/" .. name, {
      runargs = table.join({ path.join(os.scriptdir(), script) }, args or {}),
      rundir = path.join(os.scriptdir(), path.directory(script)),
      run_timeout = 30000,
    })
  end
  local function core(name, script, args)
    case("pipelined_core/" .. name, "pipelined_core/" .. script .. ".lua", args)
  end
  core("stack_remap_table", "stack_remap_table")
  core("ghost_scheduler_ooo", "ghost_scheduler")
  core("coalescing_lsu_outstanding", "lsu_outstanding")
  for _, nw in ipairs({ 8, 16, 32 }) do
    for _, ns in ipairs({ 1, 2, 4 }) do
      local args = { "--num-warps", tostring(nw), "--num-subcores", tostring(ns) }
      core(string.format("scoreboard/w%d_s%d", nw, ns), "scoreboard", args)
      for _, width in ipairs({ 1, 2, 4 }) do
        for _, iwis in ipairs({ false, true }) do
          core(
            string.format("frontend/w%d_s%d_f%d", nw, ns, width) .. (iwis and "_iwis" or ""),
            "frontend",
            table.join(args, {
              "--fetch-width",
              tostring(width),
              "--decode-width",
              tostring(width),
              "--total-issued",
              "100",
            }, iwis and { "--enable-iwis" } or {})
          )
        end
      end
      for _, lanes in ipairs({ 4, 32 }) do
        core(
          string.format("simple_arbitrator/w%d_s%d_l%d", nw, ns, lanes),
          "arbitrator",
          table.join(
            args,
            { "--num-lanes", tostring(lanes), "--total-issued", "100", "--arbitrator", "simple" }
          )
        )
      end
    end
  end
  for _, nw in ipairs({ 4, 8, 16, 32 }) do
    for _, ns in ipairs({ 1, 2, 4, 8 }) do
      for _, nl in ipairs({ 4, 32 }) do
        for _, arch in ipairs({ "Baseline", "DuplicateSRF" }) do
          for _, banks in ipairs({ 1, 2, 4 }) do
            for _, rcu in ipairs({ 2, 4, 8 }) do
              for _, wcu in ipairs({ 1, 2 }) do
                if nw % ns == 0 and banks * ns <= nw and rcu <= banks and wcu <= banks then
                  for _, sp in ipairs({ 0, 1, 2 }) do
                    for _, rp in ipairs(sp == 0 and { 1, 2, 3 } or { 0 }) do
                      for _, wp in ipairs(sp == 0 and { 1, 2 } or { 0 }) do
                        local name = string.format(
                          "pipelined_arbitrator/w%d_s%d_l%d_rcu%d_wcu%d_b%d_sp%d_rp%d_wp%d_%s",
                          nw,
                          ns,
                          nl,
                          rcu,
                          wcu,
                          banks,
                          sp,
                          rp,
                          wp,
                          arch
                        )
                        core(name, "arbitrator", {
                          "--num-warps",
                          tostring(nw),
                          "--num-subcores",
                          tostring(ns),
                          "--num-lanes",
                          tostring(nl),
                          "--num-read-collect-units",
                          tostring(rcu),
                          "--num-write-collect-units",
                          tostring(wcu),
                          "--num-regfile-banks",
                          tostring(banks),
                          "--num-shared-ports",
                          tostring(sp),
                          "--num-read-ports",
                          tostring(rp),
                          "--num-write-ports",
                          tostring(wp),
                          "--total-issued",
                          "100",
                          "--arbitrator",
                          "pipelined",
                          "--arbitrator-config",
                          arch,
                        })
                      end
                    end
                  end
                end
              end
            end
          end
        end
      end
    end
  end
  for _, lsu in ipairs({ "simple", "coalescing_outstanding" }) do
    for _, lanes in ipairs({ 4, 8, 32, 64 }) do
      core(
        string.format("lsu/%s/l%d", lsu, lanes),
        "lsu",
        { "--lsu", lsu, "--num-lanes", tostring(lanes), "--total-iter", "1000" }
      )
    end
  end
  for _, sm in ipairs({ "atomic", "pipelined" }) do
    case("sm/map/" .. sm, "sm/sm_map.lua", { "simtix." .. sm .. "_sm" })
  end
  for _, variant in ipairs({ "default", "core", "four" }) do
    case("sm/config/pipelined/" .. variant, "sm/sm_config.lua", { variant })
  end
  for lat = 1, 16 do
    for thi = 1, 16 do
      if lat % thi == 0 then
        case(string.format("utils/delay_queue/l%d_ti%d", lat, thi), "utils/delay_queue.lua", {
          "--latency",
          tostring(lat),
          "--ticks-per-output",
          tostring(thi),
          "--total-issued",
          "1000",
          "--seed",
          "48763",
        })
      end
    end
  end
  for _, name in ipairs({ "read", "non_cacheable", "flush", "invalidate" }) do
    case("cache/" .. name, "cache/cache_" .. name .. ".lua")
  end
  case("cache/banked", "cache/banked_cache.lua")
  case("cache/banked_mmio", "cache/banked_cache_mmio.lua")
  for _, name in ipairs({ "write", "random" }) do
    for _, repl in ipairs(name == "write" and { "fifo", "lru" } or { "fifo", "lru", "random" }) do
      for _, hit in ipairs({ "WriteBack", "WriteThrough" }) do
        for _, miss in ipairs({ "WriteAllocate", "WriteNoAllocate" }) do
          case(
            "cache/" .. name .. "/" .. table.concat({ repl, hit, miss }, "_"),
            "cache/cache_" .. name .. ".lua",
            { "--replacement_policy", repl, "--write_hit_policy", hit, "--write_miss_policy", miss }
          )
        end
      end
    end
  end
end)

for _, name in ipairs({
  "tag_array",
  "data_array",
  "mem_payload",
  "param",
  "mshr_file",
  "mshr_file_module",
  "write_buffer_module",
  "victim_buffer_module",
  "cache_module_basic",
  "cache_module_memory_response",
  "cache_module_hazard",
  "cache_module_atomic",
  "cache_module_command",
  "cache_module_watchdog",
}) do
  target("simtix-cache-" .. name, function()
    set_kind("binary")
    add_files("cache/" .. name .. "_test.cc")
    add_deps("simtix")
    if not name:find("module", 1, true) then add_deps("catch2") end
    add_tests("test", { run_timeout = 30000 })
  end)
end
