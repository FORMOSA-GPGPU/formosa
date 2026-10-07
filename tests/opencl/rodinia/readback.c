// SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
//
// SPDX-License-Identifier: Apache-2.0

// External validation adapter: record completed, blocking buffer transfers.
// It observes the original host's inputs and outputs without modifying data.
#define _GNU_SOURCE
#include <CL/cl.h>
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>

static void record_buffer(const char *directory, const char *prefix,
                          unsigned int sequence, size_t size,
                          const void *source) {
  char filename[4096];
  int length = snprintf(filename, sizeof(filename), "%s/%s-%03u.bin", directory,
                        prefix, sequence);
  if (length < 0 || (size_t)length >= sizeof(filename)) abort();
  FILE *file = fopen(filename, "wb");
  if (!file || fwrite(source, 1, size, file) != size || fclose(file)) {
    fprintf(stderr, "Rodinia buffer recording failed\n");
    abort();
  }
}

cl_int clEnqueueReadBuffer(cl_command_queue queue, cl_mem buffer,
                           cl_bool blocking, size_t offset, size_t size,
                           void *destination, cl_uint count,
                           const cl_event *waits, cl_event *event) {
  static __typeof__(&clEnqueueReadBuffer) original;
  static unsigned int sequence;
  if (!original) original = dlsym(RTLD_NEXT, "clEnqueueReadBuffer");
  if (!original) return CL_INVALID_OPERATION;
  cl_int status = original(queue, buffer, blocking, offset, size, destination,
                           count, waits, event);
  const char *directory = getenv("FORMOSA_BENCHMARK_READBACK");
  if (status == CL_SUCCESS && blocking && directory) {
    record_buffer(directory, "readback", sequence++, size, destination);
  }
  return status;
}

cl_int clEnqueueWriteBuffer(cl_command_queue queue, cl_mem buffer,
                            cl_bool blocking, size_t offset, size_t size,
                            const void *source, cl_uint count,
                            const cl_event *waits, cl_event *event) {
  static __typeof__(&clEnqueueWriteBuffer) original;
  static unsigned int sequence;
  if (!original) original = dlsym(RTLD_NEXT, "clEnqueueWriteBuffer");
  if (!original) return CL_INVALID_OPERATION;
  cl_int status = original(queue, buffer, blocking, offset, size, source, count,
                           waits, event);
  const char *directory = getenv("FORMOSA_BENCHMARK_UPLOAD");
  if (status == CL_SUCCESS && blocking && directory) {
    record_buffer(directory, "upload", sequence++, size, source);
  }
  return status;
}
