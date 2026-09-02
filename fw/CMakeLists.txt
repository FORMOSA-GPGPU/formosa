# SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
#
# SPDX-License-Identifier: Apache-2.0

project(gpufw LANGUAGES C ASM)

# pocl will add_subdirectory and propagate some compile flags down the stream
# remove unwanted flags
string(REPLACE "-pie" "" CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS}")
set(CMAKE_POSITION_INDEPENDENT_CODE OFF)

option(FW_ENABLE_LOG "Enable logging in firmware" OFF)
option(FW_ENABLE_TRACE "Enable tracing in firmware" OFF)

message(STATUS "FW_ENABLE_LOG=${FW_ENABLE_LOG}")
message(STATUS "FW_ENABLE_TRACE=${FW_ENABLE_TRACE}")

option(FW_ENABLE_TESTING "Build tests for firmware" OFF)
if (FW_ENABLE_TESTING)
  add_subdirectory(tests)
endif()

find_program(RV64_CC NAMES "riscv64-none-elf-gcc")
set(CMAKE_C_STANDARD 17)

# The firmware is cross-compiled for RISC-V with riscv64-none-elf-gcc. On macOS
# CMake injects Apple host flags the cross gcc/lld reject; route the compiler
# through a shim that strips them (see cmake/AppleCrossCc.cmake). No-op on Linux.
apple_cross_cc_shim(CMAKE_C_COMPILER "${RV64_CC}")
set(CMAKE_ASM_COMPILER "${CMAKE_C_COMPILER}")

# On macOS the host ar/ranlib (Apple cctools) produce archives whose index
# members lld reports as "neither ET_REL nor LLVM bitcode" when linking the
# RISC-V firmware. Use the RISC-V binutils archivers so the firmware libraries
# are well-formed ELF archives. On Linux the host GNU ar already handles this.
if(APPLE)
  find_program(RV64_AR NAMES "riscv64-none-elf-ar")
  find_program(RV64_RANLIB NAMES "riscv64-none-elf-ranlib")
  set(CMAKE_AR "${RV64_AR}")
  set(CMAKE_RANLIB "${RV64_RANLIB}")
endif()

set(RISCV_ARCH_FLAGS
  -march=rv64imc_zicsr
  -mabi=lp64
  -mcmodel=medany
)

add_executable(gpufw.elf
  freertos/common/start.S
  main.c
  cp_context.c
  cp_status.c
  cp_reboot.c
  cp_memory_copy.c
  cp_hwinfo.c
  cp_lmem_allocator.c
  cp_retire.c
  cp_dispatch.c
  cp_kernel_dispatch.c
  cp_wgi_buffer.c
  cp_stack_remap.c
  cp_trace.c
  cp_moving_average.c
  cp_panic.c
)

target_include_directories(gpufw.elf PRIVATE
  ${formosa_SOURCE_DIR}/hal/include
  ${formosa_SOURCE_DIR}/hal/src/hal
  ${formosa_SOURCE_DIR}/addr_map
)
target_link_libraries(gpufw.elf PRIVATE
  nnlc
)

target_compile_options(gpufw.elf PRIVATE
  ${RISCV_ARCH_FLAGS}
  -ffunction-sections
  -fdata-sections
  -ggdb3
  -Os
)

target_compile_definitions(gpufw.elf PRIVATE
  CP_LMEM_ALLOCATOR_USE_FREERTOS
  $<$<BOOL:${FW_ENABLE_LOG}>:FW_ENABLE_LOG>
  $<$<BOOL:${FW_ENABLE_TRACE}>:FW_ENABLE_TRACE>
)

set(FW_LINKER_SCRIPT "${CMAKE_CURRENT_LIST_DIR}/freertos/common/linker.ld")

target_link_options(gpufw.elf PRIVATE
  ${RISCV_ARCH_FLAGS}
  -ffreestanding
  -flto
  -nostdlib
  -nodefaultlibs
  -fuse-ld=lld
  -static
  -Wl,--gc-sections
  -T "${FW_LINKER_SCRIPT}"
)

# Pull in the FreeRTOS helper and source. The source path remains overridable
# so Nix can provide a separately fetched kernel while local builds use the
# repository submodule.
set(FREERTOS_PATH "${formosa_SOURCE_DIR}/third-party/FreeRTOS-Kernel"
    CACHE PATH "FreeRTOS-Kernel source directory")
include("${formosa_SOURCE_DIR}/third-party/FreeRTOS-Kernel.cmake")

message(STATUS "Curr DIR: ${CMAKE_CURRENT_SOURCE_DIR}")

set(FREERTOS_CONFIG_DIR ${CMAKE_CURRENT_SOURCE_DIR}/freertos)
set(FREERTOS_PORT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/freertos/portable)

add_freertos_instance(gpufw_freertos
  KERNEL_DIR ${FREERTOS_PATH}
  CONFIG_DIR ${FREERTOS_CONFIG_DIR}
  PORT_DIR ${FREERTOS_PORT_DIR}
  HEAP_FILE ${FREERTOS_PATH}/portable/MemMang/heap_4.c
  EXTRA_COMPILER_FLAGS ${RISCV_ARCH_FLAGS} -ffreestanding -Os
)

# nnlc
add_library(nnlc
  nanolibc/c/assert.c
  nanolibc/c/ctype.c
  nanolibc/c/libc.c
  nanolibc/c/malloc.c
  nanolibc/c/stdio.c
  nanolibc/c/stdlib.c
  nanolibc/c/string.c
  nanolibc/c/strings.c
  nanolibc/c/time.c
  nanolibc/c/unistd.c
  nanolibc/c/unsup.c
  nanolibc/formosa/formosa.c
  tinyprintf/tinyprintf.c
)

target_include_directories(nnlc PUBLIC
  ${CMAKE_CURRENT_LIST_DIR}
  ${CMAKE_CURRENT_LIST_DIR}/nanolibc
  ${CMAKE_CURRENT_LIST_DIR}/nanolibc/c
  ${CMAKE_CURRENT_LIST_DIR}/nanolibc/c/include
)

target_compile_options(nnlc PRIVATE
  ${RISCV_ARCH_FLAGS}
  -ffreestanding
  -ffunction-sections
  -fdata-sections
  -ggdb3
  -Os
)

target_link_libraries(nnlc PUBLIC
  gpufw_freertos
)

add_executable(fwrom.elf
  rom/rom.S
)

target_include_directories(fwrom.elf PRIVATE
  ${formosa_SOURCE_DIR}/hal/include
  ${formosa_SOURCE_DIR}/hal/src/hal
  ${formosa_SOURCE_DIR}/addr_map
)

target_compile_options(fwrom.elf PRIVATE
  ${RISCV_ARCH_FLAGS}
  -ffreestanding
  -ffunction-sections
  -fdata-sections
  -ggdb3
  -Os
)

target_link_options(fwrom.elf PRIVATE
  ${RISCV_ARCH_FLAGS}
  -nostdlib
  -nodefaultlibs
  -fuse-ld=lld
  -static
  -Wl,--gc-sections
  -T "${CMAKE_CURRENT_LIST_DIR}/rom/linker.ld"
)
