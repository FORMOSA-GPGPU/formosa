-- SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
--
-- SPDX-License-Identifier: Apache-2.0

function main()
  return {
    "opencl-bfs/4k/simtix.pipelined_sm",
    "opencl-kmeans/simtix.pipelined_sm",
    "opencl-rodinia-hotspot/simtix.pipelined_sm",
    "opencl-parboil-sgemm/simtix.pipelined_sm",
    "opencl-parboil-spmv/simtix.pipelined_sm",
    "opencl-parboil-histo/simtix.pipelined_sm",
    "opencl-shoc-fft/simtix.pipelined_sm",
    "opencl-shoc-scan/simtix.pipelined_sm",
    "opencl-shoc-sort/simtix.pipelined_sm",
  }
end
