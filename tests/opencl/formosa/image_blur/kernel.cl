// SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
//
// SPDX-License-Identifier: Apache-2.0

__kernel void image_blur(read_only image2d_t input, write_only image2d_t output,
                         sampler_t sampler) {
  int2 xy = (int2)(get_global_id(0), get_global_id(1));
  uint4 sum = (uint4)(0);
  for (int dy = -1; dy <= 1; ++dy)
    for (int dx = -1; dx <= 1; ++dx)
      sum += read_imageui(input, sampler, xy + (int2)(dx, dy));
  write_imageui(output, xy, sum / 9);
}
