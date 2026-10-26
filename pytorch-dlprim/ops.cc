// SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
//
// SPDX-License-Identifier: Apache-2.0

#include <ATen/ATen.h>
#include <torch/library.h>

#include <dlprim/core/pointwise.hpp>

#include "utils.h"

namespace {
bool equal(const at::Tensor &self, const at::Tensor &other) {
  TORCH_CHECK(self.device() == other.device(), "equal expects the same device");
  if (self.sizes() != other.sizes()) return false;
  if (self.numel() == 0) return true;
  // ATen's bool result is host control flow; comparison and reduction stay on
  // the device, including for aliased tensors containing NaNs.
  return at::eq(self, other).reshape({-1}).all(0).item<bool>();
}

at::Tensor &pow_scalar_out(const at::Scalar &base, const at::Tensor &exponent,
                           at::Tensor &out) {
  if (out.numel() == 0) out.resize_(exponent.sizes());
  TORCH_CHECK(
      exponent.scalar_type() == at::kFloat && out.scalar_type() == at::kFloat &&
          exponent.device() == out.device() && exponent.sizes() == out.sizes(),
      "Formosa scalar pow expects matching FP32 device tensors");
  if (exponent.numel() == 0) return out;
  auto input = exponent.contiguous();
  auto result = out.is_contiguous() ? out : at::empty_like(input);
  dlprim::core::pointwise_operation(
      {ptdlprim::todp(input)}, {ptdlprim::todp(result)}, {base.toDouble()},
      "y0=pow(w0,x0);", ptdlprim::getExecutionContext(exponent));
  if (!out.is_contiguous()) out.copy_(result);
  ptdlprim::sync_if_needed(exponent.device());
  return out;
}
}  // namespace

TORCH_LIBRARY_IMPL(aten, PrivateUse1, m) {
  m.impl("equal", TORCH_FN(equal));
  m.impl("pow.Scalar_out", TORCH_FN(pow_scalar_out));
}
