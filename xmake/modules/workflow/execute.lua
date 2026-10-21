-- SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
--
-- SPDX-License-Identifier: Apache-2.0

-- Orchestration only: target dependencies and test execution stay native.
function run(resolved, options)
  assert(resolved.adapter == "xmake", "Workflow execution supports the Xmake adapter")
  local option = import("core.base.option")
  local output_flags = {}
  for _, name in ipairs({ "quiet", "verbose", "diagnosis" }) do
    if option.get(name) then table.insert(output_flags, "--" .. name) end
  end
  -- Without -P, a child selects the outermost enclosing xmake.lua, which escapes
  -- a nested worktree. Xmake expects -P directly after the task name.
  local project = { "-P", os.projectdir() }
  local args = table.join({ "f" }, project, {
    "-y",
    "-c",
    "-m",
    resolved.build_options.mode or resolved.profile,
    "-o",
    options.builddir or "build",
  })
  table.join2(args, output_flags)
  if resolved.workflow then
    table.insert(args, "--workflow=" .. resolved.workflow)
  else
    table.insert(args, "--test-suites=" .. table.concat(resolved.test_suites, ","))
  end
  if options.installdir then table.insert(args, "--installdir=" .. options.installdir) end
  table.join2(args, options.configure or {})
  os.execv(os.programfile(), args)
  local jobs = options.jobs and { "-j", options.jobs } or {}
  os.execv(os.programfile(), table.join({ "build" }, project, output_flags, jobs))
  if not options.build_only then
    -- Retain diagnostic logs even in quiet mode; Xmake owns their layout.
    -- Keep the native exit status, including test failures, for CI callers.
    local status, errors = os.execv(
      os.programfile(),
      table.join({ "test" }, project, { "-D" }, output_flags, jobs),
      { try = true }
    )
    if not status then raise(errors) end
    if status ~= 0 then os.exit(status) end
  end
end
