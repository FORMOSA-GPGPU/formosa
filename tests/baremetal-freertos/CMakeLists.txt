cmake_minimum_required(VERSION 3.22)

project(baremetal-freertos)
enable_language(ASM)

# The wrapper selects the compiler and runtime independently, including when
# a local clang install has no adjacent RISC-V sysroot.
find_program(FORMOSA_CLANG NAMES formosa-clang REQUIRED)
set(CMAKE_C_COMPILER ${FORMOSA_CLANG})
set(CMAKE_ASM_COMPILER ${CMAKE_C_COMPILER})

set(RISCV_ARCH_FLAGS
  --target=riscv64-unknown-elf
  -march=rv64im
  -mcmodel=medany
)

# set compilation flags
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -ffreestanding")
set(CMAKE_ASM_FLAGS "${CMAKE_ASM_FLAGS}")
add_compile_options(${RISCV_ARCH_FLAGS})

# include common objects and set linker script
add_subdirectory(common)

macro(baremetal_add_third_party tp_name)
  set(THIRD_PARTY_PATH "${formosa_SOURCE_DIR}/third-party/${tp_name}")
  if(EXISTS ${THIRD_PARTY_PATH}.cmake)
    include(${THIRD_PARTY_PATH}.cmake)
  else()
    add_subdirectory(${THIRD_PARTY_PATH})
  endif()
  message(STATUS "Added baremetal third-party: ${tp_name}")
endmacro()

macro(add_baremetal_program target_name)
  add_subdirectory(${target_name})

  set_target_properties(${target_name} PROPERTIES
    OUTPUT_NAME ${target_name}.elf
  )

  target_link_libraries(${target_name} PRIVATE
    common_objs
    c # for baremetal programs, link with libc
  )

  target_link_options(${target_name} PRIVATE
    ${RISCV_ARCH_FLAGS}
    -ffreestanding
    -nostdlib
    -nodefaultlibs
    -fuse-ld=lld
    -static
    -Wl,--gc-sections
    -T${LINKER_SCRIPT}
  )
endmacro()

macro(add_freertos_program target_name)
  add_baremetal_program(${target_name})
  add_freertos_instance(${target_name}_freertos
    CONFIG_DIR ${CMAKE_CURRENT_SOURCE_DIR}/${target_name}
    PORT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/portable
    EXTRA_INCLUDES
      ${CMAKE_CURRENT_SOURCE_DIR}/include
    EXTRA_COMPILER_FLAGS
      ${RISCV_ARCH_FLAGS}
      -ffreestanding
  )
  target_link_libraries(${target_name}_freertos PUBLIC lv::device)
  target_link_libraries(${target_name} PRIVATE ${target_name}_freertos)
endmacro()

# define third-party dependencies
baremetal_add_third_party(FreeRTOS-Kernel)

# define programs
add_baremetal_program(baremetal_demo)
add_freertos_program(freertos_demo)
