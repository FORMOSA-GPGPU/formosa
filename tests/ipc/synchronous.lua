-- SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
-- SPDX-License-Identifier: Apache-2.0

local args = { ... }
local U = require("posix.unistd")
local socket = os.tmpname()
local disconnect = args[4] == "disconnect"
local ready_r, ready_w = U.pipe()
local pid = U.fork()
if pid == 0 then
  U.close(ready_w)
  assert(U.read(ready_r, 1) == "1")
  U.close(ready_r)
  local code =
    os.execute(string.format("%q %q %q %q", args[1], socket, args[2], args[4] or "normal"))
  os.exit(code == 0 and 0 or 1)
end
U.close(ready_r)
local clock = sc.clock("clock", sc.time(10, sc.time_unit.NS))
local memory = simple.Memory("memory", { latency = 2, size = 256 })
memory.clock = clock
memory:write_bytes(0, { 42 })
local initiator = simple.Initiator("initiator")
initiator.clock = clock
local agent = ipc.Agent("agent", {
  socket_path = socket,
  synchronous = args[3] ~= "free-running",
  probe_hook = function() initiator:add_payload({ addr = 1, size = 1 }) end,
})
agent.target = memory.port
initiator.target = agent.port
sc.start(sc.time(70, sc.time_unit.NS))
agent:start()
assert(U.write(ready_w, "1"))
U.close(ready_w)
sc.start()
local ns = sc.time_stamp() / sc.time(1, sc.time_unit.NS)
print("IPC_ELAPSED_NS=" .. ns)
assert(
  disconnect or ns == 650,
  "host scheduling leaked into simulated time or transaction latency changed"
)
sc.start(sc.time(100, sc.time_unit.NS))
assert(sc.time_stamp() / sc.time(1, sc.time_unit.NS) == ns + 100, "drain did not advance")
print("DRAIN_RETURNED")
local bytes = initiator:get_read_data()
if disconnect then
  assert(initiator:completed_count() == 1, "disconnected reverse transaction was not cancelled")
  assert(not bytes or #bytes == 0, "disconnected reverse transaction succeeded")
else
  assert(bytes and #bytes == 1 and bytes[1] == 42, "reverse transaction failed")
end
local _, reason, code = require("posix.sys.wait").wait(pid)
assert(reason == "exited" and code == 0, "host failed")
os.remove(socket)
