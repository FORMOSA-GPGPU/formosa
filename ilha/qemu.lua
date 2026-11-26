-- SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
--
-- SPDX-License-Identifier: Apache-2.0

-- Boot a minimal Linux guest (tools/qemu-guest.nix) in QEMU with the device
-- attached as a vfio-user PCI endpoint. The simulation runs in this process
-- until the guest powers off. Given a command, the guest runs it as root from
-- the current directory, and this script exits with its status:
--   qemu.lua -- build/bin/opencl-vecadd
-- Without one, the guest console is a root shell.

local System = require("ilha.system")
local Host = require("ilha.host")
local argparse = require("argparse")
local ConfigCli = require("ilha.config_cli")
local S = require("posix.signal")
local stdlib = require("posix.stdlib")
local U = require("posix.unistd")
local W = require("posix.sys.wait")

local parser = argparse("qemu.lua", "Boot a Linux guest with the device on its PCI bus")
ConfigCli.add_options(parser, true)
parser:option("-s --stats", "Output path of the stat"):args(1)
parser:option("-t --trace", "Output prefix of the Perfetto trace"):args(1):default("qemu")
parser:option("--memory", "Guest RAM size"):default("4G")
parser:option("--cpus", "Guest vCPUs"):default("4")
parser:argument("command", "Command to run in the guest, then power off"):args("*")
local args = parser:parse({ ... })
local config = ConfigCli.load(args)
local interactive = #args.command == 0

trace.settings.streaming = true
trace.settings.file_write_period_ms = 100
trace.settings.flush_period_ms = 250
trace.settings.flush_period_sc_time = sc.time(100, sc.time_unit.NS)
trace.settings.buffer_size_kb = 16384
trace.settings.output_prefix = args.trace

local function quote(s) return "'" .. s:gsub("'", "'\\''") .. "'" end

local function write_file(path, content)
  local file = assert(io.open(path, "w"))
  assert(file:write(content))
  assert(file:close())
end

-- One sorted export per variable with a shell name, for the guest's init.
local function write_exports(path, variables)
  local lines = {}
  for name, value in pairs(variables) do
    if name:match("^[%a_][%w_]*$") then
      lines[#lines + 1] = ("export %s=%s\n"):format(name, quote(value))
    end
  end
  table.sort(lines)
  write_file(path, table.concat(lines))
end

-- The FORMOSA dev shell (and its Docker image) provides the guest and the
-- PoCL its applications use. A local PoCL override of the host's
-- OCL_ICD_VENDORS stays on the host; point FORMOSA_QEMU_POCL at a prefix the
-- guest can see to run another PoCL there.
local function shell_path(name)
  local value = os.getenv(name)
  assert(value and value ~= "", name .. " is not set; run qemu.lua in the FORMOSA dev shell")
  return value
end
local guest = shell_path("FORMOSA_QEMU_GUEST")
local pocl = shell_path("FORMOSA_QEMU_POCL")

-- The guest sees these host paths, each at its own path: the store, the
-- repository holding this script, and the temporary directory, where wrappers
-- keep their files.
local script = assert(stdlib.realpath(debug.getinfo(1, "S").source:sub(2)))
local source = assert(script:match("^(.*)/ilha/[^/]+$"))
local tmpdir = assert(stdlib.realpath(os.getenv("TMPDIR") or "/tmp"))
local cwd = U.getcwd()

local function shared(file)
  for _, root in ipairs({ "/nix/store", source, tmpdir }) do
    if file == root or file:sub(1, #root + 1) == root .. "/" then return true end
  end
  return false
end

-- A path the guest is given must be shared both as given and as resolved,
-- unless the guest's init provides it.
local guest_files = { ["/bin/sh"] = true, ["/usr/bin/env"] = true }
local function visible(file)
  if guest_files[file] then return true end
  local real = stdlib.realpath(file)
  -- A missing file is the guest's to report.
  if not real then return true end
  return shared(file:sub(1, 1) == "/" and file or cwd .. "/" .. file) and shared(real)
end

assert(
  visible(cwd),
  "the guest cannot see the current directory " .. cwd .. "; run under " .. source
)
local program = args.command[1]
if program and program:find("/", 1, true) then
  assert(visible(program), "the guest cannot see " .. program .. "; build it under " .. source)
end
assert(visible(pocl), "the guest cannot see FORMOSA_QEMU_POCL " .. pocl)

-- The run directory, private to this run, holds the vfio-user socket and what
-- the guest's init reads from /host: `env` to mount the shares, `environ` for
-- the command, and `cmd`. The device sits alone behind rp0, so it enumerates
-- as bus 1.
local state = assert(stdlib.mkdtemp(tmpdir .. "/formosa-qemu-XXXXXX"))
local socket_path = state .. "/vfio-user.sock"
assert(#socket_path < 108, "state directory path too long for a UNIX socket: " .. socket_path)
local pci_device = "0000:01:00.0"

write_exports(state .. "/env", {
  FORMOSA_SOURCE = source,
  FORMOSA_CWD = cwd,
  FORMOSA_TMPDIR = tmpdir,
  FORMOSA_PCI_DEVICE = pci_device,
})

-- The command runs in this process's environment, which names paths the guest
-- shares; HOME and the shell's own state stay the guest's.
local environ = stdlib.getenv()
for _, name in ipairs({ "HOME", "PWD", "OLDPWD", "SHLVL", "_" }) do
  environ[name] = nil
end
-- HAL reaches the device on the guest's PCI bus.
environ.FORMOSA_REAL = "pci"
environ.FORMOSA_PCI_DEVICE = pci_device
environ.OCL_ICD_VENDORS = pocl .. "/etc/OpenCL/vendors"
environ.POCL_DEVICES = "formosa"
write_exports(state .. "/environ", environ)

if not interactive then
  local words = {}
  for i, word in ipairs(args.command) do
    words[i] = quote(word)
  end
  write_file(state .. "/cmd", table.concat(words, " ") .. "\n")
end

-- Multi-threaded TCG now and then loses a guest timer interrupt and hangs the guest.
local accel, cpu = "tcg,thread=single", "max"
if U.access("/dev/kvm", "rw") == 0 then
  accel, cpu = "kvm", "host"
else
  io.stderr:write("warning: /dev/kvm is not accessible; the guest runs under TCG\n")
end

local function share(tag, path, readonly)
  local options = "local,path=%s,mount_tag=%s,security_model=none%s"
  return options:format(path, tag, readonly and ",readonly=on" or "")
end
local endpoint = '{"driver":"vfio-user-pci","bus":"rp0","socket":{"path":"%s","type":"unix"}}'

-- A q35 PC that boots the guest, one option and its value per line.
local qemu_options = {
  { "-nodefaults" },
  { "-no-reboot" },
  { "-display", "none" },
  -- In the interactive shell Ctrl-C belongs to the guest; with a command it
  -- stops this run.
  { "-chardev", "stdio,id=console,signal=" .. (interactive and "off" or "on") },
  { "-serial", "chardev:console" },
  { "-machine", "q35,memory-backend=mem" },
  { "-accel", accel },
  { "-cpu", cpu },
  { "-smp", args.cpus },
  { "-m", args.memory },
  -- Device DMA needs guest RAM shared with the simulator.
  { "-object", "memory-backend-memfd,id=mem,size=" .. args.memory .. ",share=on" },
  { "-kernel", guest .. "/bzImage" },
  { "-initrd", guest .. "/initrd" },
  { "-append", "console=ttyS0 quiet panic=-1" },
  { "-virtfs", share("nixstore", "/nix/store", true) },
  { "-virtfs", share("formosa", source) },
  { "-virtfs", share("tmp", tmpdir) },
  { "-virtfs", share("host", state, true) },
  -- The guest reports the command's status through this port.
  { "-device", "isa-debug-exit,iobase=0xf4,iosize=0x04" },
  -- virtio-iommu notifies every unmap with the range it mapped, as vfio-user
  -- requires.
  { "-device", "virtio-iommu-pci" },
  -- Behind a root port the device enumerates like a card in a slot, not a
  -- Root Complex Integrated Endpoint.
  { "-device", "pcie-root-port,id=rp0,chassis=1" },
  { "-device", endpoint:format(socket_path) },
}
local qemu_argv = {}
for _, option in ipairs(qemu_options) do
  table.move(option, 1, #option, #qemu_argv + 1, qemu_argv)
end

-- Fork QEMU before the simulator starts its server thread, and hold it until
-- the vfio-user socket exists: the returned function lets it exec.
local function fork_qemu(argv)
  local go_r, go_w = U.pipe()
  local pid = assert(U.fork())
  if pid == 0 then
    U.close(go_w)
    -- End of file: this process stopped before the socket existed.
    if U.read(go_r, 1) ~= "1" then U._exit(1) end
    U.execp("qemu-system-x86_64", argv)
    U._exit(127)
  end
  U.close(go_r)
  return pid, function()
    assert(U.write(go_w, "1"))
    U.close(go_w)
  end
end

-- The guest's status from how QEMU stopped. The guest's poweroff exits 0; with
-- a command, isa-debug-exit exits (value << 1) | 1, where value is status + 1.
local function guest_status(kind, code)
  local exited = kind == "exited"
  if interactive then
    if exited and code == 0 then return 0 end
    io.stderr:write(("QEMU stopped abnormally: %s %s\n"):format(kind, code))
  elseif exited and code % 2 == 1 and code >= 3 then
    return (code - 1) / 2 - 1
  else
    io.stderr:write("the guest stopped without reporting the command's status\n")
  end
  return 1
end

local qemu, start_qemu = fork_qemu(qemu_argv)
local system = System("System", config, { host = Host.pcie(socket_path) })
start_qemu()

-- Simulate until QEMU stops or this run is interrupted.
local interrupted = false
S.signal(S.SIGINT, function() interrupted = true end)
S.signal(S.SIGTERM, function() interrupted = true end)
local status = 1
while true do
  system:start(100000)
  local pid, kind, code = W.wait(qemu, W.WNOHANG)
  if pid == qemu then
    status = guest_status(kind, code)
    break
  elseif interrupted then
    S.kill(qemu, S.SIGTERM)
    W.wait(qemu)
    break
  end
end

os.execute("rm -rf " .. quote(state))
if args.stats then write_file(args.stats, system.stats:dump_toml()) end
sc.stop()
if status ~= 0 then os.exit(status) end
