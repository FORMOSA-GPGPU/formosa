# SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
#
# SPDX-License-Identifier: Apache-2.0

import ctypes

import torch
import pytorch_ocl


monitor = ctypes.CDLL(None)
monitor.dlprim_monitor_initialize.restype = ctypes.c_int
monitor.dlprim_monitor_transfers.argtypes = [ctypes.c_bool]
monitor.dlprim_monitor_transfers.restype = None
monitor.dlprim_monitor_counters.argtypes = [ctypes.POINTER(ctypes.c_uint64)]
monitor.dlprim_monitor_counters.restype = None


def counters():
    values = (ctypes.c_uint64 * 3)()
    monitor.dlprim_monitor_counters(values)
    return tuple(values)


torch.set_num_threads(1)
torch.autograd.set_multithreading_enabled(False)
assert monitor.dlprim_monitor_initialize() == 0
parameter = torch.nn.Parameter(torch.tensor([1.0, 2.0], device="ocl:0"))
x = torch.tensor([3.0, 4.0], device="ocl:0")
optimizer = torch.optim.SGD([parameter], lr=0.1, foreach=False)
torch.ocl.synchronize()
before = counters()
monitor.dlprim_monitor_transfers(False)
try:
    loss = (parameter * x).sum()
    loss.backward()
    optimizer.step()
    torch.ocl.synchronize()
finally:
    monitor.dlprim_monitor_transfers(True)
after = counters()
assert after[0] > before[0], "No OpenCL kernel execution"
assert after[1:] == before[1:], "CPU computation or tensor transfer"
for actual, expected in (
    (loss, torch.tensor(11.0)),
    (parameter.grad, torch.tensor([3.0, 4.0])),
    (parameter, torch.tensor([0.7, 1.6])),
):
    assert actual.device.type == "ocl"
    torch.testing.assert_close(actual.detach().cpu(), expected)
print("PASS: forward, backward, SGD; CPU fallback=0", flush=True)
