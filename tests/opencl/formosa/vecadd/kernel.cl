__kernel void vecadd (__global const int *A,
	                    __global const int *B,
	                    __global int *C)
{
  int gid = get_global_id(0);
  C[gid] = A[gid] + B[gid];
}

__kernel void vecadd_float (__global const float *A,
	                          __global const float *B,
	                          __global float *C)
{
  int gid = get_global_id(0);
  C[gid] = A[gid] + B[gid];
}

__kernel void vecadd_double (__global const double *A,
                             __global const double *B,
                             __global double *C)
{
  int gid = get_global_id(0);
  C[gid] = A[gid] + B[gid];
}
