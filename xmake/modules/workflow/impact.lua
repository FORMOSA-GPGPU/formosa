-- SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
--
-- SPDX-License-Identifier: Apache-2.0

local frontend = import("workflow.frontend")

local function read_binary(filename)
  local file = assert(io.open(filename, "rb"))
  local contents = file:read("*a")
  file:close()
  return contents
end

local function git(rootdir, arguments, allow_failure)
  local stdout_file = os.tmpfile()
  local stderr_file = os.tmpfile()
  local status = os.execv("git", arguments, {
    curdir = rootdir,
    stderr = stderr_file,
    stdout = stdout_file,
    try = true,
  })
  local stdout = read_binary(stdout_file)
  local stderr = read_binary(stderr_file)
  os.rm(stdout_file)
  os.rm(stderr_file)
  if status ~= 0 then
    if allow_failure then return nil, stderr end
    raise("Git command failed: %s", stderr)
  end
  return stdout, stderr
end

local function nonempty_environment(name)
  local value = os.getenv(name)
  if value and value ~= "" then return value end
end

local function infer_base(rootdir)
  -- Compose the private-provider marker so public export can keep this module neutral.
  local change_base = nonempty_environment("CI_" .. "MERGE" .. "_REQUEST_DIFF_BASE_SHA")
  if change_base then return change_base end

  local previous_revision = nonempty_environment("CI_COMMIT_BEFORE_SHA")
  if previous_revision then
    if previous_revision:match("^0+$") then
      raise("CI_COMMIT_BEFORE_SHA has no parent revision; provide --base")
    end
    return previous_revision
  end

  change_base = nonempty_environment("WORKFLOW_DIFF_BASE_SHA")
  if change_base then return change_base end

  previous_revision = nonempty_environment("WORKFLOW_PREVIOUS_REVISION")
  if previous_revision then
    if previous_revision:match("^0+$") then
      raise("WORKFLOW_PREVIOUS_REVISION has no parent revision; provide --base")
    end
    return previous_revision
  end

  local remote_ref = git(rootdir, { "symbolic-ref", "--quiet", "refs/remotes/origin/HEAD" }, true)
  if not remote_ref or remote_ref:match("^%s*$") then
    raise("Cannot infer the remote default branch; provide --base")
  end
  remote_ref = remote_ref:gsub("%s+$", "")
  local base = git(rootdir, { "merge-base", remote_ref, "HEAD" })
  return base:gsub("%s+$", "")
end

local function nul_fields(output)
  local fields = {}
  local offset = 1
  while offset <= #output do
    local terminator = output:find("\0", offset, true)
    if not terminator then
      fields[#fields + 1] = output:sub(offset)
      break
    end
    fields[#fields + 1] = output:sub(offset, terminator - 1)
    offset = terminator + 1
  end
  return fields
end

local function append_name_status(paths, seen, output)
  local fields = nul_fields(output)
  local index = 1
  while index <= #fields do
    local status = fields[index]
    index = index + 1
    local path_count = status:match("^[RC]") and 2 or 1
    for _ = 1, path_count do
      local changed_path = fields[index]
      if changed_path and changed_path ~= "" and not seen[changed_path] then
        paths[#paths + 1] = changed_path
        seen[changed_path] = true
      end
      index = index + 1
    end
  end
end

local function changed_files(rootdir, options)
  local head = options.head or "HEAD"
  if options.working_tree and options.head and options.head ~= "HEAD" then
    raise("--working-tree cannot be combined with a non-HEAD --head")
  end
  local base = options.base or infer_base(rootdir)
  git(rootdir, { "rev-parse", "--verify", base .. "^{commit}" })
  git(rootdir, { "rev-parse", "--verify", head .. "^{commit}" })

  local paths = {}
  local seen = {}
  append_name_status(
    paths,
    seen,
    git(rootdir, { "diff", "--name-status", "-z", "--find-renames", "--find-copies", base, head })
  )
  if options.working_tree then
    append_name_status(
      paths,
      seen,
      git(rootdir, { "diff", "--cached", "--name-status", "-z", "--find-renames", "--find-copies" })
    )
    append_name_status(
      paths,
      seen,
      git(rootdir, { "diff", "--name-status", "-z", "--find-renames", "--find-copies" })
    )
    for _, changed_path in
      ipairs(nul_fields(git(rootdir, { "ls-files", "--others", "--exclude-standard", "-z" })))
    do
      if changed_path ~= "" and not seen[changed_path] then
        paths[#paths + 1] = changed_path
        seen[changed_path] = true
      end
    end
  end
  table.sort(paths)
  return paths, base, head
end

local function matches(pattern, changed_path)
  return changed_path:match("^" .. path.pattern(pattern) .. "$") ~= nil
end

function matching_paths(paths, patterns)
  local matched = {}
  for _, changed_path in ipairs(paths) do
    for _, pattern in ipairs(patterns) do
      if matches(pattern, changed_path) then
        matched[#matched + 1] = changed_path
        break
      end
    end
  end
  return matched
end

local function direct_components(config, files)
  local matched = {}
  local matched_files = {}
  for _, changed_path in ipairs(files) do
    for _, component_name in ipairs(frontend.keys(config.components)) do
      for _, pattern in ipairs(config.components[component_name].paths) do
        if matches(pattern, changed_path) then
          matched[component_name] = true
          matched_files[changed_path] = true
          break
        end
      end
    end
  end
  return frontend.keys(matched), matched_files
end

local function affected_components(config, direct)
  local reverse = {}
  for name in pairs(config.components) do
    reverse[name] = {}
  end
  for name, component in pairs(config.components) do
    for _, dependency in ipairs(component.depends) do
      reverse[dependency][#reverse[dependency] + 1] = name
    end
  end
  for _, dependents in pairs(reverse) do
    table.sort(dependents)
  end

  local affected = {}
  local queue = {}
  for _, name in ipairs(direct) do
    affected[name] = true
    queue[#queue + 1] = name
  end
  local index = 1
  while index <= #queue do
    local name = queue[index]
    index = index + 1
    for _, dependent in ipairs(reverse[name]) do
      if not affected[dependent] then
        affected[dependent] = true
        queue[#queue + 1] = dependent
      end
    end
  end
  return frontend.keys(affected), affected
end

local function selected_suites(config, affected)
  local selected = {}
  for suite_name, suite in pairs(config.test_suites) do
    for _, dependency in ipairs(suite.depends) do
      if affected[dependency] then
        selected[suite_name] = true
        break
      end
    end
  end
  return frontend.keys(selected), selected
end

local function affected_workflows(config, selected)
  local affected = {}
  for workflow_name, workflow in pairs(config.workflows) do
    for _, suite_name in ipairs(workflow.test_suites) do
      if selected[suite_name] then
        affected[workflow_name] = true
        break
      end
    end
  end
  return frontend.keys(affected)
end

function analyze(config, rootdir, options)
  local files, base, head = changed_files(rootdir, options)
  local direct, matched_files = direct_components(config, files)
  local affected, affected_set = affected_components(config, direct)
  local suites, suite_set = selected_suites(config, affected_set)
  local workflows = affected_workflows(config, suite_set)
  local unmatched = {}
  for _, changed_path in ipairs(files) do
    if not matched_files[changed_path] then unmatched[#unmatched + 1] = changed_path end
  end
  return {
    affected_components = affected,
    affected_workflows = workflows,
    base = base,
    changed_files = files,
    direct_components = direct,
    head = head,
    test_suites = suites,
    unmatched_files = unmatched,
  }
end

return {
  analyze = analyze,
  matching_paths = matching_paths,
}
