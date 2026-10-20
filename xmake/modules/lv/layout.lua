-- SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
--
-- SPDX-License-Identifier: Apache-2.0

-- Shared by runtime defaults, development staging and native installation.
function directories()
  return {
    bin = "bin",
    lua = "share/lv/lua",
    runfiles = "share/lv/runfiles",
    scripts = "share/lv/scripts",
  }
end

function runtime_root()
  import("core.project.project")
  return path.absolute(
    path.join(project.target("lv"):targetdir(), path.relative(".", directories().bin))
  )
end

function runtime_defines(target)
  local dirs = directories()
  target:add("defines", 'LV_DEFAULT_RUNTIMEPATH="' .. path.relative(dirs.lua, dirs.bin) .. '"')
  target:add(
    "defines",
    'LV_DEFAULT_RUNFILES_ROOT="' .. path.relative(dirs.runfiles, dirs.bin) .. '"'
  )
end

-- Generated artifacts retain their logical runfile paths after being linked in bin/.
function stage_runfile(source, logical)
  source = path.absolute(source)
  local destination = path.join(runtime_root(), directories().runfiles, logical)
  if
    not os.islink(destination)
    or path.absolute(os.readlink(destination), path.directory(destination)) ~= source
  then
    os.tryrm(destination)
    os.mkdir(path.directory(destination))
    os.ln(source, destination)
  end
end

-- Build launchers execute live source; install launchers use prefix-relative scripts.
function script_launcher(prefix, script)
  local launcher = path.join(prefix, directories().bin, path.filename(script))
  local quoted = "'" .. script:gsub("'", "'\\''") .. "'"
  local argument = path.is_absolute(script) and quoted or '"$bindir"/' .. quoted
  local contents = "#!/bin/sh\n"
    .. 'bindir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd) || exit\n'
    .. 'exec "$bindir/lv" '
    .. argument
    .. ' "$@"\n'
  if not os.isfile(launcher) or io.readfile(launcher) ~= contents then
    os.mkdir(path.directory(launcher))
    io.writefile(launcher, contents)
  end
  os.runv("chmod", { "+x", launcher })
end
