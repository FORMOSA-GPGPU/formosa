# SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
#
# SPDX-License-Identifier: Apache-2.0

include(CheckLanguage)

# Check CUDA
check_language(CUDA)

if(CMAKE_CUDA_COMPILER)
  message(STATUS "Check if host supports CUDA")
  enable_language(CUDA)

  set(DETECT_SCRIPT "${CMAKE_CURRENT_BINARY_DIR}/detect_cuda_arch.cu")
  file(WRITE ${DETECT_SCRIPT} "
      #include <stdio.h>
      #include <cuda_runtime.h>
      int main() {
          int count = 0;
          if (cudaGetDeviceCount(&count) != cudaSuccess || count == 0) {
              return 1; // No GPU found
          }
          cudaDeviceProp prop;
          cudaGetDeviceProperties(&prop, 0); // Get properties of the first GPU
          printf(\"%d%d\", prop.major, prop.minor);
          return 0;
      }
  ")

  # Detect nixGL wrapper if needed for Nix-based environments
  find_program(NIXGL_NVIDIA nixgl-nvidia)
  if(NIXGL_NVIDIA)
    set(OLD_EMULATOR "${CMAKE_CROSSCOMPILING_EMULATOR}")
    set(CMAKE_CROSSCOMPILING_EMULATOR "${NIXGL_NVIDIA}")
  endif()

  try_run(RUN_RESULT COMPILE_RESULT
      ${CMAKE_CURRENT_BINARY_DIR} ${DETECT_SCRIPT}
      RUN_OUTPUT_VARIABLE DETECTED_ARCH
  )

  if(COMPILE_RESULT AND RUN_RESULT EQUAL 0)
    set(HOST_HAS_CUDA TRUE)
    # Extract only the numeric part (last line or sequence of digits)
    # This handles Nix output like "building... \n 89"
    string(REGEX MATCH "[0-9]+$" CLEAN_ARCH "${DETECTED_ARCH}")
    if(CLEAN_ARCH)
      set(CMAKE_CUDA_ARCHITECTURES ${CLEAN_ARCH} CACHE STRING "CUDA architectures" FORCE)
    else()
      # Fallback to whatever was detected if regex fails
      set(CMAKE_CUDA_ARCHITECTURES ${DETECTED_ARCH} CACHE STRING "CUDA architectures" FORCE)
    endif()
  else()
    set(HOST_HAS_CUDA FALSE)
  endif()

  # Restore or clean up emulator
  if(NIXGL_NVIDIA)
    set(CMAKE_CROSSCOMPILING_EMULATOR "${OLD_EMULATOR}")
  endif()
else()
  set(HOST_HAS_CUDA FALSE)
endif()

message(STATUS "CUDA Available:               ${HOST_HAS_CUDA}")
if(HOST_HAS_CUDA)
  message(STATUS "CUDA Compute Architecture:    ${CMAKE_CUDA_ARCHITECTURES}")
endif()
