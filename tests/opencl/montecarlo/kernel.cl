__kernel void montecarlo(__global int *result) {
  int x = get_global_id(0);
  int y = get_global_id(1);
  int sx = get_global_size(0);
  int sy = get_global_size(1);
  int id = y * sx + x;
  result[id] = (x * x + y * y) < sx * sx;
}

__kernel void all_one(__global int *rx, __global int *ry, __global int *rsx) {
  int x = get_global_id(0);
  int y = get_global_id(1);
  int sx = get_global_size(0);
  int id = y * sx + x;
  rx[id] = x;
  ry[id] = y;
  rsx[id] = sx;
}

__kernel void all_zero(__global int *result) {
  int x = get_global_id(0);
  int y = get_global_id(1);
  int sx = get_global_size(0);
  int id = y * sx + x;
  result[id] = 0;
}
