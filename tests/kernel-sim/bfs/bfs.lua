-- SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
--
-- SPDX-License-Identifier: Apache-2.0

local ffi = require("ffi")
local util = require("lv.util")
local args = { ... }

---@type kernel-sim.runtime
local ksim = require("kernel-sim.runtime")

---@type kernel-sim.system
local system = require("kernel-sim.system")("bfs.lua", args)

ffi.cdef([[
typedef struct {
  int starting;
  int no_of_edges;
} node_t;

typedef struct {
  node_t *graph_nodes;
  int *graph_edges;
  int *graph_mask;
  int *updating_graph_mask;
  int *graph_visited;
  int *cost;
  int *over;
  int no_of_nodes;
  int phase;
} kargs_t;
]])

math.randomseed(os.time())

local function generate_csr_graph(no_of_nodes, max_out_edges)
  local adjacency = {}
  local adjacency_set = {}
  local degree_count = {}
  local target_degree = {}

  for i = 0, no_of_nodes - 1 do
    adjacency[i] = {}
    adjacency_set[i] = {}
    degree_count[i] = 0
    target_degree[i] = math.random(1, max_out_edges)
  end

  -- Add edges in pairs: if u -> v then v -> u.
  local max_pass = no_of_nodes * max_out_edges * 4
  for _ = 1, max_pass do
    local progress = false

    for u = 0, no_of_nodes - 1 do
      while degree_count[u] < target_degree[u] do
        local found = false

        for _ = 1, no_of_nodes * 2 do
          local v = math.random(0, no_of_nodes - 1)
          if v ~= u and degree_count[v] < target_degree[v] and not adjacency_set[u][v] then
            adjacency_set[u][v] = true
            adjacency_set[v][u] = true
            table.insert(adjacency[u], v)
            table.insert(adjacency[v], u)
            degree_count[u] = degree_count[u] + 1
            degree_count[v] = degree_count[v] + 1
            progress = true
            found = true
            break
          end
        end

        if not found then break end
      end
    end

    if not progress then break end
  end

  local edge_list_size = 0
  for i = 0, no_of_nodes - 1 do
    edge_list_size = edge_list_size + degree_count[i]
  end

  local graph_nodes = ffi.new("node_t[?]", no_of_nodes)
  local graph_edges = ffi.new("int[?]", edge_list_size)

  local edge_cursor = 0
  for i = 0, no_of_nodes - 1 do
    graph_nodes[i].starting = edge_cursor
    graph_nodes[i].no_of_edges = degree_count[i]
    for _, dst in ipairs(adjacency[i]) do
      graph_edges[edge_cursor] = dst
      edge_cursor = edge_cursor + 1
    end
  end

  return graph_nodes, graph_edges, edge_list_size
end

local function dump_csr_graph(path, no_of_nodes, graph_nodes, edge_list_size, graph_edges, source)
  local f = assert(io.open(path, "w"), "failed to open graph output file: " .. path)

  f:write(string.format("%d\n", no_of_nodes))
  for i = 0, no_of_nodes - 1 do
    f:write(string.format("%d %d\n", graph_nodes[i].starting, graph_nodes[i].no_of_edges))
  end

  f:write("\n")
  f:write(string.format("%d\n\n", source))
  f:write(string.format("%d\n", edge_list_size))
  for i = 0, edge_list_size - 1 do
    f:write(string.format("%d %d\n", graph_edges[i], 1))
  end

  f:close()
end

local function bfs_cpu_reference(no_of_nodes, graph_nodes, graph_edges, source)
  local dist = {}
  for i = 0, no_of_nodes - 1 do
    dist[i] = -1
  end

  local queue = { source }
  local q_head = 1
  dist[source] = 0

  while q_head <= #queue do
    local u = queue[q_head]
    q_head = q_head + 1

    local start = graph_nodes[u].starting
    local finish = start + graph_nodes[u].no_of_edges
    for edge_idx = start, finish - 1 do
      local v = graph_edges[edge_idx]
      if dist[v] == -1 then
        dist[v] = dist[u] + 1
        queue[#queue + 1] = v
      end
    end
  end

  return dist
end

-- Compare-result dump is intentionally disabled.
local function dump_compare_result(path, no_of_nodes, cpu_cost, gpu_cost)
  local f = assert(io.open(path, "w"), "failed to open compare output file: " .. path)
  f:write("node_id,cpu_cost,gpu_cost,match\n")
  for i = 0, no_of_nodes - 1 do
    local cpu_val = cpu_cost[i]
    local gpu_val = gpu_cost[i]
    local matched = (cpu_val == gpu_val) and 1 or 0
    f:write(string.format("%d,%d,%d,%d\n", i, cpu_val, gpu_val, matched))
  end
  f:close()
end

local function align_up(addr, align) return math.floor((addr + align - 1) / align) * align end

local no_of_nodes = 100
local max_out_edges = 5
local source = 0

local random_graph_output_path = util.runfile("tests/kernel-sim/bfs/random_graph.txt")

local graph_nodes, graph_edges, edge_list_size
graph_nodes, graph_edges, edge_list_size = generate_csr_graph(no_of_nodes, max_out_edges)
dump_csr_graph(
  random_graph_output_path,
  no_of_nodes,
  graph_nodes,
  edge_list_size,
  graph_edges,
  source
)
local expected_cost = bfs_cpu_reference(no_of_nodes, graph_nodes, graph_edges, source)

local graph_mask = ffi.new("int[?]", no_of_nodes)
local updating_graph_mask = ffi.new("int[?]", no_of_nodes)
local graph_visited = ffi.new("int[?]", no_of_nodes)
local cost = ffi.new("int[?]", no_of_nodes)
local over = ffi.new("int[1]", { 0 })

for i = 0, no_of_nodes - 1 do
  graph_mask[i] = 0
  updating_graph_mask[i] = 0
  graph_visited[i] = 0
  cost[i] = -1
end

graph_mask[source] = 1
graph_visited[source] = 1
cost[source] = 0

local base_addr = ksim.scratch_base
local graph_nodes_addr = base_addr
local graph_edges_addr = align_up(graph_nodes_addr + ffi.sizeof("node_t") * no_of_nodes, 64)
local graph_mask_addr = align_up(graph_edges_addr + ffi.sizeof("int") * edge_list_size, 64)
local updating_graph_mask_addr = align_up(graph_mask_addr + ffi.sizeof("int") * no_of_nodes, 64)
local graph_visited_addr = align_up(updating_graph_mask_addr + ffi.sizeof("int") * no_of_nodes, 64)
local cost_addr = align_up(graph_visited_addr + ffi.sizeof("int") * no_of_nodes, 64)
local over_addr = align_up(cost_addr + ffi.sizeof("int") * no_of_nodes, 64)
local kargs_addr = align_up(over_addr + ffi.sizeof("int"), 64)

ksim.upload_data(system, graph_nodes_addr, graph_nodes)
ksim.upload_data(system, graph_edges_addr, graph_edges)
ksim.upload_data(system, graph_mask_addr, graph_mask)
ksim.upload_data(system, updating_graph_mask_addr, updating_graph_mask)
ksim.upload_data(system, graph_visited_addr, graph_visited)
ksim.upload_data(system, cost_addr, cost)
ksim.upload_data(system, over_addr, over)

local kargs = ffi.new("kargs_t", {
  graph_nodes = ffi.cast("node_t*", graph_nodes_addr),
  graph_edges = ffi.cast("int*", graph_edges_addr),
  graph_mask = ffi.cast("int*", graph_mask_addr),
  updating_graph_mask = ffi.cast("int*", updating_graph_mask_addr),
  graph_visited = ffi.cast("int*", graph_visited_addr),
  cost = ffi.cast("int*", cost_addr),
  over = ffi.cast("int*", over_addr),
  no_of_nodes = no_of_nodes,
  phase = 0,
})

local kernel_elf = util.runfile("bin/bfs.kernel-sim.elf")
local iteration = 0
repeat
  over[0] = 0
  ksim.upload_data(system, over_addr, over)

  kargs.phase = 0
  ksim.upload_data(system, kargs_addr, kargs)
  ksim.run_kernel(system, kernel_elf, kargs_addr, no_of_nodes, system.num_threads)

  kargs.phase = 1
  ksim.upload_data(system, kargs_addr, kargs)
  ksim.run_kernel(system, kernel_elf, kargs_addr, no_of_nodes, system.num_threads)

  ksim.download_data(system, over_addr, over)
  iteration = iteration + 1
until over[0] == 0

local cost_result = ffi.new("int[?]", no_of_nodes)
ksim.download_data(system, cost_addr, cost_result)

-- Dump comparing result file for debugging
-- dump_compare_result("kernel-sim.txt", no_of_nodes, expected_cost, cost_result)

for i = 0, no_of_nodes - 1 do
  local actual = cost_result[i]
  local expected = expected_cost[i]
  print(string.format("node=%2d, actual=%3d, expected=%3d", i, actual, expected))
  assert(actual == expected)
end

print("Pass!")
