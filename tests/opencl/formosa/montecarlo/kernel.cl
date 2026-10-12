// PCG RXS-M-XS hash for parallel pseudorandom sampling:
// https://jcgt.org/published/0009/03/02/
static uint pcg_hash(uint input) {
  uint state = input * 747796405u + 2891336453u;
  uint word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
  return (word >> 22u) ^ word;
}

__kernel void montecarlo(__global int *result, uint n_points, uint seed) {
  size_t id = get_global_id(0);
  if (id >= n_points) return;

  uint counter = 2u * (uint)id + pcg_hash(seed);
  // Uniform 24-bit coordinates represent x,y in [0,1). Compare the squared
  // distance in integer arithmetic so CPU and GPU agree at the circle boundary.
  ulong x = pcg_hash(counter) >> 8u;
  ulong y = pcg_hash(counter + 1u) >> 8u;
  result[id] = x * x + y * y < (1UL << 48u);
}
