-- SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
--
-- SPDX-License-Identifier: Apache-2.0

target("lv-axi-test", function()
  set_kind("binary")
  add_files("axi_adapters_test.cc", "axi_safety_test.cc", "axi_reset_test.cc")
  add_files("$(projectdir)/lv/src/lv/bindings/simple/memory.cc")
  add_includedirs("$(projectdir)")
  add_deps("liblv", "elfio")
  local no_gaps =
    { LV_AXI_WRITE_RESP_GAP = "0", LV_AXI_READ_BEAT_GAP = "0", LV_AXI_READ_BURST_GAP = "0" }
  add_tests("adapters", { runenvs = no_gaps })
  add_tests("write-aw-after-w", { runargs = { "--axi-write-aw-after-w" } })
  add_tests("initiator-reset", { runargs = { "--axi-initiator-reset" } })
  for _, operation in ipairs({ "read", "write" }) do
    add_tests("target-reset/" .. operation, { runargs = { "--axi-target-reset", operation } })
  end
  for _, reset in ipairs({ "reset", "no-reset" }) do
    add_tests("write-response-gap/" .. reset, {
      runargs = { "--axi-write-response-gap", reset },
      runenvs = { LV_AXI_WRITE_RESP_GAP = "8" },
    })
  end
  for _, old_op in ipairs({ "read", "write" }) do
    for _, new_op in ipairs({ "read", "write" }) do
      for _, channel in ipairs({ "same-channel", "cross-channel" }) do
        add_tests("reset/serialization/old-" .. old_op .. "/new-" .. new_op .. "/" .. channel, {
          runargs = { "--axi-reset-serialization", old_op, new_op, channel },
          runenvs = no_gaps,
        })
      end
    end
  end
  for split = 0, 1 do
    local split_name = split == 1 and "split" or "no-split"
    for read = 0, 1 do
      local operation = read == 1 and "read" or "write"
      for _, case in ipairs({
        "plain9",
        "plain3",
        "short",
        "long",
        "zero",
        "plain-zero",
        "null",
        "size",
        "huge-burst",
        "cross",
        "later-cross",
        "address-overflow",
        "address-width",
        "byte-zero",
        "byte1",
        "byte2",
        "byte3",
        "plain-valid",
        "null-enables",
        "streaming-zero",
        "streaming-short",
        "streaming-equal",
        "streaming-long",
        "unaligned",
        "aligned-narrow",
        "wrap-target",
      }) do
        add_tests("safety/" .. case .. "/" .. split_name .. "/" .. operation, {
          runargs = { "--axi-safety", case, tostring(split), tostring(read), "2" },
        })
      end
      for _, metadata in ipairs({ "plain", "extension" }) do
        for _, case in ipairs({ "max-beats", "too-many-beats", "page-end", "page-cross" }) do
          add_tests(
            "geometry/" .. case .. "/" .. metadata .. "/" .. split_name .. "/" .. operation,
            {
              runargs = {
                "--axi-safety",
                "geometry-" .. metadata .. "-" .. case,
                tostring(split),
                tostring(read),
                "2",
              },
            }
          )
        end
      end
    end
  end
  for _, case in ipairs({ "absent-bid", "valid-read" }) do
    add_tests(
      "protocol/" .. case,
      { runargs = { "--axi-safety", case .. "-response", "0", "0", "2" } }
    )
  end
end)

-- Invalid transactions must be rejected with the deliberate fatal exit (23)
-- and its diagnostic. An unrelated failure or timeout must not pass the test.
target("lv-axi-rejection-test", function()
  set_kind("phony")
  add_deps("lv-axi-test")
  local function add_rejection_test(name, opt)
    add_tests(name, {
      runargs = {
        "--axi-safety",
        opt.scenario,
        opt.split and "1" or "0",
        opt.read and "1" or "0",
        tostring(opt.beats or 2),
      },
      run_timeout = 15000,
      expected_message = opt.expected_message,
    })
  end
  for read = 0, 1 do
    local operation = read == 1 and "read" or "write"
    add_rejection_test("wrap/initiator/" .. operation, {
      scenario = "wrap-initiator",
      split = false,
      read = read == 1,
      beats = 4,
      expected_message = "AXI->TLM " .. operation .. ": WRAP AXI bursts are unsupported",
    })
    add_rejection_test("unaligned/initiator/" .. operation, {
      scenario = "unaligned-initiator",
      split = false,
      read = read == 1,
      beats = 2,
      expected_message = "AXI->TLM " .. operation .. ": unaligned AXI beats are unsupported",
    })
    for split = 0, 1 do
      for _, side in ipairs({ "target", "initiator" }) do
        if side == "target" or split == 0 then
          local direction = side == "target" and "TLM->AXI" or "AXI->TLM"
          local split_name = split == 1 and "split" or "no-split"
          for beats = 1, 2 do
            add_rejection_test(
              "fixed/" .. side .. "/" .. split_name .. "/" .. operation .. "/" .. beats .. "-beat",
              {
                scenario = "fixed-" .. side,
                split = split == 1,
                read = read == 1,
                beats = beats,
                expected_message = direction
                  .. " "
                  .. operation
                  .. ": FIXED AXI bursts are unsupported",
              }
            )
          end
        end
      end
    end
    for case, reason in pairs({
      ["oversized"] = "AxSIZE exceeds data bus width",
      ["raw-size"] = "AxSIZE exceeds data bus width",
      ["len"] = "AxLEN exceeds 255",
      ["raw-len"] = "AxLEN exceeds 255",
      ["page"] = "burst crosses 4-KiB boundary",
      ["range"] = "burst exceeds address range",
      ["early-wlast"] = "WLAST does not match AWLEN",
      ["missing-wlast"] = "WLAST does not match AWLEN",
    }) do
      if read == 0 or not case:find("wlast", 1, true) then
        add_rejection_test("protocol/" .. case .. "/" .. operation, {
          scenario = case .. "-initiator",
          split = false,
          read = read == 1,
          beats = 2,
          expected_message = "AXI->TLM " .. operation .. ": " .. reason,
        })
      end
    end
  end
  for case, reason in pairs({
    ["wrong-rid"] = "read: RID does not match ARID",
    ["late-wrong-rid"] = "read: RID does not match ARID",
    ["wrong-bid"] = "write: BID does not match AWID",
    ["early-rlast"] = "read: RLAST does not match ARLEN",
    ["missing-rlast"] = "read: RLAST does not match ARLEN",
  }) do
    add_rejection_test("protocol/" .. case, {
      scenario = case .. "-response",
      expected_message = "TLM->AXI " .. reason,
    })
  end
  add_rejection_test("protocol/missing-wlast/single-beat", {
    scenario = "missing-wlast-initiator",
    split = false,
    read = false,
    beats = 1,
    expected_message = "AXI->TLM write: WLAST does not match AWLEN",
  })
  on_test(function(target, opt)
    local stdout, stderr = os.tmpfile(), os.tmpfile()
    local code, errors =
      os.execv(path.absolute(target:dep("lv-axi-test"):targetfile()), opt.runargs, {
        try = true,
        timeout = opt.run_timeout,
        stdout = stdout,
        stderr = stderr,
      })
    opt.stdout = os.isfile(stdout) and io.readfile(stdout) or ""
    opt.stderr = os.isfile(stderr) and io.readfile(stderr) or ""
    os.tryrm(stdout)
    os.tryrm(stderr)
    local log = opt.stdout .. "\n" .. opt.stderr
    local passed = code == 23 and log:find(opt.expected_message, 1, true) ~= nil
    for _, forbidden in ipairs({
      "runtime error:",
      "UndefinedBehaviorSanitizer",
      "AddressSanitizer",
      "TIMEOUT",
    }) do
      if log:find(forbidden, 1, true) then passed = false end
    end
    if not passed then
      opt.errors = "Expected fatal exit 23 matching "
        .. opt.expected_message
        .. ", got "
        .. tostring(code)
        .. ": "
        .. tostring(errors or "")
    end
    return passed
  end)
end)
